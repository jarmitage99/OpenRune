#include "engine/net.h"

#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/world.h"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace rune::net {
    namespace {
        Driver* activeDriver = nullptr;

        constexpr uint8_t MessageHello = 1;
        constexpr uint8_t MessageWelcome = 2;
        constexpr uint8_t MessageJoin = 3;
        constexpr uint8_t MessageOpen = 4;
        constexpr uint8_t MessageUpdate = 5;
        constexpr uint8_t MessageClose = 6;
        constexpr uint8_t MessageCall = 7;
        constexpr uint8_t MessagePossess = 8;
        constexpr uint8_t MessageBye = 9;

        constexpr uint8_t RefNone = 0;
        constexpr uint8_t RefChannel = 1;
        constexpr uint8_t RefPath = 2;

        constexpr double ReplicationInterval = 1.0 / 30.0;
        constexpr double RelevantTimeout = 5.0;
        constexpr size_t MaxMessage = 1 << 22;

        struct Out {
            std::vector<uint8_t> data;

            void u8(uint8_t value)
            {
                data.push_back(value);
            }

            void u16(uint16_t value)
            {
                u8(static_cast<uint8_t>(value & 255));
                u8(static_cast<uint8_t>(value >> 8));
            }

            void u32(uint32_t value)
            {
                for (int i = 0; i < 4; i++) {
                    u8(static_cast<uint8_t>((value >> (8 * i)) & 255));
                }
            }

            void f32(float value)
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                u32(bits);
            }

            void str(const std::string& value)
            {
                u32(static_cast<uint32_t>(value.size()));
                data.insert(data.end(), value.begin(), value.end());
            }

            void bytes(const std::vector<uint8_t>& value)
            {
                data.insert(data.end(), value.begin(), value.end());
            }
        };

        struct In {
            const uint8_t* data = nullptr;
            size_t size = 0;
            size_t position = 0;

            void need(size_t count)
            {
                if (position + count > size) {
                    throw std::runtime_error("truncated network message");
                }
            }

            uint8_t u8()
            {
                need(1);
                return data[position++];
            }

            uint16_t u16()
            {
                uint16_t low = u8();
                return static_cast<uint16_t>(low | (u8() << 8));
            }

            uint32_t u32()
            {
                uint32_t value = 0;
                for (int i = 0; i < 4; i++) {
                    value |= static_cast<uint32_t>(u8()) << (8 * i);
                }
                return value;
            }

            float f32()
            {
                uint32_t bits = u32();
                float value = 0;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            std::string str()
            {
                uint32_t length = u32();
                need(length);
                std::string value(reinterpret_cast<const char*>(data + position), length);
                position += length;
                return value;
            }
        };

        void message(Connection& connection, uint8_t type, const Out& body)
        {
            Out header;
            header.u32(static_cast<uint32_t>(body.data.size()));
            header.u8(type);
            connection.output.insert(connection.output.end(), header.data.begin(), header.data.end());
            connection.output.insert(connection.output.end(), body.data.begin(), body.data.end());
        }

        bool isActor(UObject* object)
        {
            return object && object->isA(engineClasses().actor);
        }

        UObject* topOwner(UObject* actor)
        {
            UObject* top = actor;
            for (int guard = 0; guard < 32 && actor::Owner(top); guard++) {
                top = actor::Owner(top);
            }
            return top;
        }

        bool ownedBy(UObject* actor, UObject* owner)
        {
            for (UObject* current = actor; current; current = actor::Owner(current)) {
                if (current == owner) {
                    return true;
                }
                if (current == actor::Owner(current)) {
                    break;
                }
            }
            return false;
        }

        UClass* ownerClass(UObject* field)
        {
            for (UObject* outer = field; outer; outer = outer->outer) {
                if (UClass* cls = cast<UClass>(outer)) {
                    return cls;
                }
            }
            return nullptr;
        }

        bool condition(UObject* actor, UField* field, uint16_t offset)
        {
            UClass* owner = ownerClass(field);
            if (!owner || offset >= owner->script.size()) {
                return false;
            }
            Frame frame;
            frame.object = actor;
            frame.node = owner;
            frame.code = owner->script.data() + offset;
            uint32_t value = 0;
            try {
                frame.step(actor, &value);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "replication condition of %s failed: %s\n", field->name.c_str(), error.what());
                return false;
            }
            return value != 0;
        }

        void writeRef(Out& out, Connection& connection, UObject* object, bool& resolved)
        {
            if (!object || object->destroyed) {
                out.u8(RefNone);
                return;
            }
            if (isActor(object)) {
                auto it = connection.channels.find(object);
                if (it != connection.channels.end()) {
                    out.u8(RefChannel);
                    out.u32(it->second.id);
                    return;
                }
                if (!actor::bStatic(object) && !actor::bNoDelete(object)) {
                    resolved = false;
                    out.u8(RefNone);
                    return;
                }
            }
            out.u8(RefPath);
            out.str(object->pathName());
        }

        UObject* readRef(In& in, Connection& connection)
        {
            uint8_t kind = in.u8();
            if (kind == RefChannel) {
                auto it = connection.byId.find(in.u32());
                return it == connection.byId.end() || it->second->destroyed ? nullptr : it->second;
            }
            if (kind == RefPath) {
                std::string path = in.str();
                UObject* found = objects::findObject(path, nullptr);
                if (!found) {
                    found = objects::loadObject(path, nullptr);
                    objects::flushLoads();
                }
                return found;
            }
            return nullptr;
        }

        void writeValue(Out& out, Connection& connection, const UProperty* property, const uint8_t* address, bool& resolved)
        {
            switch (property->type) {
            case PropType::Byte:
                out.u8(*address);
                break;
            case PropType::Int:
                out.u32(*reinterpret_cast<const uint32_t*>(address));
                break;
            case PropType::Bool:
                out.u8(*reinterpret_cast<const uint32_t*>(address) != 0 ? 1 : 0);
                break;
            case PropType::Float:
                out.f32(*reinterpret_cast<const float*>(address));
                break;
            case PropType::Name:
                out.str(reinterpret_cast<const Name*>(address)->str());
                break;
            case PropType::Str:
            case PropType::String:
                out.str(*reinterpret_cast<const std::string*>(address));
                break;
            case PropType::Object:
            case PropType::Class:
                writeRef(out, connection, *reinterpret_cast<UObject* const*>(address), resolved);
                break;
            case PropType::Struct:
                for (UProperty* member : property->structType->properties) {
                    for (int32_t i = 0; i < member->arrayDim; i++) {
                        writeValue(out, connection, member, address + member->offset + static_cast<size_t>(i) * member->elementSize(), resolved);
                    }
                }
                break;
            case PropType::Array:
            case PropType::Map:
            case PropType::FixedArray:
            case PropType::Pointer:
                break;
            }
        }

        void readValue(In& in, Connection& connection, const UProperty* property, uint8_t* address)
        {
            switch (property->type) {
            case PropType::Byte:
                *address = in.u8();
                break;
            case PropType::Int:
                *reinterpret_cast<uint32_t*>(address) = in.u32();
                break;
            case PropType::Bool:
                *reinterpret_cast<uint32_t*>(address) = in.u8();
                break;
            case PropType::Float:
                *reinterpret_cast<float*>(address) = in.f32();
                break;
            case PropType::Name:
                *reinterpret_cast<Name*>(address) = Name(in.str());
                break;
            case PropType::Str:
            case PropType::String:
                *reinterpret_cast<std::string*>(address) = in.str();
                break;
            case PropType::Object:
            case PropType::Class:
                *reinterpret_cast<UObject**>(address) = readRef(in, connection);
                break;
            case PropType::Struct:
                for (UProperty* member : property->structType->properties) {
                    for (int32_t i = 0; i < member->arrayDim; i++) {
                        readValue(in, connection, member, address + member->offset + static_cast<size_t>(i) * member->elementSize());
                    }
                }
                break;
            case PropType::Array:
            case PropType::Map:
            case PropType::FixedArray:
            case PropType::Pointer:
                break;
            }
        }

        bool remoteCallHook(UObject* object, UFunction* function, uint8_t* parameters)
        {
            return activeDriver && activeDriver->remoteCall(object, function, parameters);
        }

        void setNetSpeed(UObject* player)
        {
            int32_t speed = player::ConfiguredLanSpeed(player);
            player::CurrentNetSpeed(player) = speed > 0 ? speed : 20000;
        }
    }

    Driver* active()
    {
        return activeDriver;
    }

    bool isHostUrl(const std::string& map)
    {
        if (map.empty()) {
            return false;
        }
        if (map.find(':') != std::string::npos || map == "localhost") {
            return true;
        }
        int dots = 0;
        for (char c : map) {
            if (c == '.') {
                dots++;
            } else if (c < '0' || c > '9') {
                return false;
            }
        }
        return dots == 3;
    }

    Driver::~Driver()
    {
        for (auto& connection : connections) {
            Out body;
            body.str("");
            message(*connection, MessageBye, body);
            flush(*connection);
        }
        if (activeDriver == this) {
            activeDriver = nullptr;
            vm::setRemoteCallHook(nullptr);
        }
    }

    std::unique_ptr<Driver> Driver::listen(uint16_t port, std::string& error)
    {
        std::unique_ptr<Driver> driver(new Driver());
        driver->isServer = true;
        if (!driver->listener.open(Socket::Kind::Tcp) || !driver->listener.bind(port) || !driver->listener.listen()) {
            error = "cannot listen on port " + std::to_string(port);
            return nullptr;
        }
        driver->listenPort = driver->listener.localPort();
        activeDriver = driver.get();
        vm::setRemoteCallHook(remoteCallHook);
        return driver;
    }

    std::unique_ptr<Driver> Driver::connect(const std::string& host, uint16_t port, const std::string& options, std::string& error)
    {
        uint32_t ip = 0;
        if (!resolve(host, ip)) {
            error = "cannot resolve " + host;
            return nullptr;
        }
        std::unique_ptr<Driver> driver(new Driver());
        auto connection = std::make_unique<Connection>();
        connection->address = { ip, port };
        if (!connection->socket.open(Socket::Kind::Tcp) || !connection->socket.connect(connection->address)) {
            error = "cannot connect to " + host + ":" + std::to_string(port);
            return nullptr;
        }
        Out body;
        body.u32(ProtocolVersion);
        body.str(options);
        message(*connection, MessageHello, body);
        driver->connections.push_back(std::move(connection));
        activeDriver = driver.get();
        vm::setRemoteCallHook(remoteCallHook);
        return driver;
    }

    bool Driver::server() const
    {
        return isServer;
    }

    uint16_t Driver::port() const
    {
        return listenPort;
    }

    size_t Driver::connectionCount() const
    {
        return connections.size();
    }

    bool Driver::disconnected() const
    {
        return !isServer && (connections.empty() || connections.front()->closing);
    }

    bool Driver::takeWelcome(std::string& map)
    {
        if (welcomeMap.empty()) {
            return false;
        }
        map = welcomeMap;
        welcomeMap.clear();
        return true;
    }

    std::string Driver::reason() const
    {
        return connections.empty() ? std::string("connection closed") : connections.front()->options;
    }

    Connection* Driver::connectionOf(UObject* actor)
    {
        for (auto& connection : connections) {
            if (connection->actor == actor) {
                return connection.get();
            }
        }
        return nullptr;
    }

    void Driver::attach(World* target)
    {
        world = target;
        for (auto& connection : connections) {
            connection->channels.clear();
            connection->byId.clear();
            connection->tornOff.clear();
            connection->actor = nullptr;
            if (isServer) {
                connection->joined = false;
                if (connection->welcomed && world) {
                    Out body;
                    body.str(world->url.substr(0, world->url.find('?')));
                    message(*connection, MessageWelcome, body);
                }
            }
        }
    }

    bool Driver::waitForWelcome(double seconds, std::string& map, std::string& error)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            if (connections.empty()) {
                break;
            }
            Connection& connection = *connections.front();
            flush(connection);
            receive(connection);
            if (connection.closing) {
                error = connection.options.empty() ? "connection lost" : connection.options;
                return false;
            }
            if (!welcomeMap.empty()) {
                map = welcomeMap;
                welcomeMap.clear();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        error = "no answer from server";
        return false;
    }

    void Driver::join()
    {
        if (connections.empty()) {
            return;
        }
        message(*connections.front(), MessageJoin, Out {});
        flush(*connections.front());
    }

    void Driver::tick(float deltaSeconds)
    {
        static const Name nameLogout("Logout");
        time += static_cast<double>(deltaSeconds);
        if (isServer) {
            acceptConnections();
        }
        for (size_t i = 0; i < connections.size(); i++) {
            receive(*connections[i]);
        }
        if (isServer) {
            if (time >= nextReplication) {
                nextReplication = time + ReplicationInterval;
                replicateServer();
            }
        } else {
            replicateClient();
        }
        for (auto it = connections.begin(); it != connections.end();) {
            Connection& connection = **it;
            flush(connection);
            if (isServer && connection.closing) {
                if (world && connection.actor && !connection.actor->destroyed) {
                    UObject* pawn = connection.actor;
                    if (world->gameInfo) {
                        vm::Call(world->gameInfo, nameLogout).obj(pawn).run();
                    }
                    if (!pawn->destroyed) {
                        world->destroy(pawn);
                    }
                }
                objects::destroyObject(connection.player);
                it = connections.erase(it);
                continue;
            }
            ++it;
        }
    }

    void Driver::acceptConnections()
    {
        for (int i = 0; i < 8; i++) {
            auto connection = std::make_unique<Connection>();
            if (!listener.accept(connection->socket, connection->address)) {
                return;
            }
            connections.push_back(std::move(connection));
        }
    }

    void Driver::receive(Connection& connection)
    {
        uint8_t buffer[65536];
        for (;;) {
            int64_t count = connection.socket.receive(buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            connection.input.insert(connection.input.end(), buffer, buffer + count);
        }
        if (connection.socket.failed()) {
            connection.closing = true;
        }
        size_t position = 0;
        while (connection.input.size() - position >= 5 && !connection.closing) {
            uint32_t size = static_cast<uint32_t>(connection.input[position]) | static_cast<uint32_t>(connection.input[position + 1]) << 8 | static_cast<uint32_t>(connection.input[position + 2]) << 16 | static_cast<uint32_t>(connection.input[position + 3]) << 24;
            if (size > MaxMessage) {
                connection.closing = true;
                break;
            }
            if (connection.input.size() - position < 5 + static_cast<size_t>(size)) {
                break;
            }
            uint8_t type = connection.input[position + 4];
            std::vector<uint8_t> body(connection.input.begin() + static_cast<ptrdiff_t>(position + 5), connection.input.begin() + static_cast<ptrdiff_t>(position + 5 + size));
            position += 5 + size;
            try {
                process(connection, type, body.data(), body.size());
            } catch (const std::exception& error) {
                std::fprintf(stderr, "bad network message %u: %s\n", type, error.what());
                connection.closing = true;
            }
        }
        connection.input.erase(connection.input.begin(), connection.input.begin() + static_cast<ptrdiff_t>(position));
    }

    void Driver::flush(Connection& connection)
    {
        while (!connection.output.empty()) {
            int64_t sent = connection.socket.send(connection.output.data(), connection.output.size());
            if (sent <= 0) {
                break;
            }
            connection.output.erase(connection.output.begin(), connection.output.begin() + static_cast<ptrdiff_t>(sent));
        }
        if (connection.socket.failed()) {
            connection.closing = true;
        }
    }

    void Driver::process(Connection& connection, uint8_t type, const uint8_t* data, size_t size)
    {
        static const Name namePreLogin("PreLogin");
        In in { data, size };
        switch (type) {
        case MessageHello: {
            if (!isServer || connection.welcomed) {
                return;
            }
            uint32_t version = in.u32();
            connection.options = in.str();
            std::string error;
            if (version != ProtocolVersion) {
                error = "protocol mismatch";
            } else if (world && world->gameInfo) {
                vm::Call preLogin(world->gameInfo, namePreLogin);
                if (preLogin.valid()) {
                    preLogin.s(connection.options).s(ipText(connection.address.ip)).s("").s("").run();
                    error = *reinterpret_cast<std::string*>(preLogin.parameter(2));
                }
            }
            if (!error.empty()) {
                Out body;
                body.str(error);
                message(connection, MessageBye, body);
                connection.closing = true;
                return;
            }
            connection.welcomed = true;
            Out body;
            body.str(world ? world->url.substr(0, world->url.find('?')) : std::string());
            message(connection, MessageWelcome, body);
            return;
        }
        case MessageWelcome:
            if (!isServer) {
                welcomeMap = in.str();
            }
            return;
        case MessageJoin:
            if (isServer && connection.welcomed && !connection.joined) {
                login(connection);
            }
            return;
        case MessageOpen:
            if (!isServer) {
                receiveOpen(connection, data, size);
            }
            return;
        case MessageUpdate:
            receiveUpdate(connection, data, size);
            return;
        case MessageClose:
            if (!isServer) {
                receiveClose(connection, data, size);
            }
            return;
        case MessageCall:
            receiveCall(connection, data, size);
            return;
        case MessagePossess:
            if (!isServer) {
                receivePossess(connection, data, size);
            }
            return;
        case MessageBye:
            connection.options = in.str();
            connection.closing = true;
            return;
        default:
            throw std::runtime_error("unknown message type");
        }
    }

    void Driver::login(Connection& connection)
    {
        static const Name nameNetConnection("NetConnection");
        static const Name nameTransient("Transient");
        if (!world) {
            return;
        }
        if (!connection.player) {
            connection.player = objects::newObject(objects::intrinsicClass(nameNetConnection), objects::packageObject(nameTransient), Name(), 0);
        }
        setNetSpeed(connection.player);
        std::string error;
        UObject* pawn = world->spawnPlayActor(connection.player, ROLE_AutonomousProxy, connection.options, error);
        if (!pawn) {
            Out body;
            body.str(error.empty() ? "login failed" : error);
            message(connection, MessageBye, body);
            connection.closing = true;
            return;
        }
        connection.joined = true;
        connection.actor = pawn;
        openChannel(connection, pawn);
        Channel& channel = connection.channels[pawn];
        replicate(connection, channel, true);
        Out body;
        body.u32(channel.id);
        message(connection, MessagePossess, body);
    }

    bool Driver::relevant(Connection& connection, UObject* actor, int depth)
    {
        UObject* viewer = connection.actor;
        if (actor::bAlwaysRelevant(actor) || !viewer) {
            return actor::bAlwaysRelevant(actor) != 0;
        }
        if (actor == viewer || ownedBy(actor, viewer) || actor == actor::Instigator(viewer)) {
            return true;
        }
        UObject* owner = actor::Owner(actor);
        if (owner && owner != actor && depth < 32 && (actor::Base(actor) == owner || actor::AttachParent(actor) == owner) && !actor::bOnlyOwnerSee(actor)) {
            return relevant(connection, owner, depth + 1);
        }
        if ((actor::bHidden(actor) || actor::bOnlyOwnerSee(actor)) && !actor::bBlockPlayers(actor) && !actor::AmbientSound(actor)) {
            return false;
        }
        Vector eye = actor::Location(viewer);
        if (isPawn(viewer)) {
            eye.z += pawn::BaseEyeHeight(viewer);
        }
        return world->fastLineCheck(actor::Location(actor), eye);
    }

    void Driver::openChannel(Connection& connection, UObject* actor)
    {
        Channel channel;
        channel.id = connection.nextId++;
        channel.actor = actor;
        channel.lastRelevant = time;
        Out body;
        body.u32(channel.id);
        bool existing = actor::bStatic(actor) || actor::bNoDelete(actor);
        body.u8(existing ? 1 : 0);
        body.str(existing ? actor->pathName() : actor->cls->pathName());
        body.f32(actor::Location(actor).x);
        body.f32(actor::Location(actor).y);
        body.f32(actor::Location(actor).z);
        body.u32(static_cast<uint32_t>(actor::Rotation(actor).pitch));
        body.u32(static_cast<uint32_t>(actor::Rotation(actor).yaw));
        body.u32(static_cast<uint32_t>(actor::Rotation(actor).roll));
        uint8_t remoteRole = actor::RemoteRole(actor);
        if (remoteRole == ROLE_AutonomousProxy && topOwner(actor) != connection.actor) {
            remoteRole = ROLE_SimulatedProxy;
        }
        body.u8(remoteRole);
        message(connection, MessageOpen, body);
        connection.byId[channel.id] = actor;
        connection.channels[actor] = std::move(channel);
    }

    void Driver::closeChannel(Connection& connection, UObject* actor, bool keepActor)
    {
        auto it = connection.channels.find(actor);
        if (it == connection.channels.end()) {
            return;
        }
        Out body;
        body.u32(it->second.id);
        body.u8(keepActor ? 1 : 0);
        message(connection, MessageClose, body);
        connection.byId.erase(it->second.id);
        connection.channels.erase(it);
    }

    void Driver::actorDestroyed(UObject* actor)
    {
        for (auto& connection : connections) {
            connection->tornOff.erase(actor);
            if (isServer) {
                closeChannel(*connection, actor, false);
            } else {
                auto it = connection->channels.find(actor);
                if (it != connection->channels.end()) {
                    connection->byId.erase(it->second.id);
                    connection->channels.erase(it);
                }
            }
            if (connection->actor == actor) {
                connection->actor = nullptr;
            }
        }
    }

    void Driver::replicate(Connection& connection, Channel& channel, bool toClient)
    {
        UObject* actor = channel.actor;
        uint8_t remoteRole = actor::RemoteRole(actor);
        if (toClient) {
            bool owner = topOwner(actor) == connection.actor;
            actor::bNetOwner(actor) = owner ? 1 : 0;
            actor::bNetInitial(actor) = channel.initial ? 1 : 0;
            actor::bNetRelevant(actor) = 1;
            actor::bNetSee(actor) = 1;
            actor::bNetHear(actor) = 1;
            actor::bNetFeel(actor) = 1;
            actor::bSimulatedPawn(actor) = isPawn(actor) && remoteRole == ROLE_SimulatedProxy ? 1 : 0;
            if (remoteRole == ROLE_AutonomousProxy && !owner) {
                actor::RemoteRole(actor) = ROLE_SimulatedProxy;
                actor::bSimulatedPawn(actor) = isPawn(actor) ? 1 : 0;
            }
        }
        std::unordered_map<const UField*, bool> conditions;
        Out entries;
        uint16_t count = 0;
        const std::vector<UProperty*>& properties = actor->cls->properties;
        for (size_t index = 0; index < properties.size(); index++) {
            UProperty* property = properties[index];
            if (!(property->propertyFlags & CPF_Net)) {
                continue;
            }
            UClass* owner = ownerClass(property);
            auto known = conditions.find(property);
            bool send = false;
            if (known != conditions.end()) {
                send = known->second;
            } else {
                send = condition(actor, owner ? static_cast<UField*>(owner) : property, property->replicationOffset);
                conditions[property] = send;
            }
            if (!send) {
                continue;
            }
            for (int32_t element = 0; element < property->arrayDim; element++) {
                size_t offset = property->offset + static_cast<size_t>(element) * property->elementSize();
                Out value;
                bool resolved = true;
                writeValue(value, connection, property, actor->data + offset, resolved);
                if (!resolved) {
                    continue;
                }
                uint32_t key = static_cast<uint32_t>(index) << 8 | static_cast<uint32_t>(element);
                auto sent = channel.sent.find(key);
                if (sent == channel.sent.end()) {
                    Out defaults;
                    bool defaultResolved = true;
                    writeValue(defaults, connection, property, actor->cls->defaultObject->data + offset, defaultResolved);
                    bool spawnedByClass = !actor::bStatic(actor) && !actor::bNoDelete(actor);
                    if (spawnedByClass && defaultResolved && defaults.data == value.data) {
                        channel.sent[key] = value.data;
                        continue;
                    }
                } else if (sent->second == value.data) {
                    continue;
                }
                channel.sent[key] = value.data;
                entries.u16(static_cast<uint16_t>(index));
                entries.u8(static_cast<uint8_t>(element));
                entries.bytes(value.data);
                count++;
            }
        }
        if (toClient) {
            actor::RemoteRole(actor) = remoteRole;
            actor::bNetInitial(actor) = 0;
        }
        channel.initial = false;
        if (count == 0) {
            return;
        }
        Out body;
        body.u32(channel.id);
        body.u16(count);
        body.bytes(entries.data);
        message(connection, MessageUpdate, body);
    }

    void Driver::replicateServer()
    {
        if (!world) {
            return;
        }
        std::vector<UObject*> actors = world->actors;
        for (auto& owned : connections) {
            Connection& connection = *owned;
            if (!connection.joined || connection.closing) {
                continue;
            }
            for (UObject* actor : actors) {
                if (!actor || actor->destroyed || actor::RemoteRole(actor) == ROLE_None || connection.tornOff.count(actor)) {
                    continue;
                }
                bool isRelevant = relevant(connection, actor);
                auto it = connection.channels.find(actor);
                if (it == connection.channels.end()) {
                    if (!isRelevant) {
                        continue;
                    }
                    openChannel(connection, actor);
                    it = connection.channels.find(actor);
                } else if (isRelevant) {
                    it->second.lastRelevant = time;
                } else if (time - it->second.lastRelevant > RelevantTimeout) {
                    closeChannel(connection, actor, false);
                    continue;
                }
                Channel& channel = it->second;
                if (time < channel.nextUpdate) {
                    continue;
                }
                float frequency = actor::NetUpdateFrequency(actor);
                channel.nextUpdate = time + (frequency > 0 ? 1.0 / static_cast<double>(frequency) : 0.0);
                replicate(connection, channel, true);
                if (actor::bNetTemporary(actor)) {
                    connection.tornOff.insert(actor);
                    closeChannel(connection, actor, true);
                }
            }
        }
    }

    void Driver::replicateClient()
    {
        if (!world || connections.empty() || !connections.front()->actor) {
            return;
        }
        Connection& connection = *connections.front();
        auto it = connection.channels.find(connection.actor);
        if (it != connection.channels.end()) {
            replicate(connection, it->second, false);
        }
    }

    void Driver::receiveOpen(Connection& connection, const uint8_t* data, size_t size)
    {
        In in { data, size };
        uint32_t id = in.u32();
        bool existing = in.u8() != 0;
        std::string path = in.str();
        Vector location { in.f32(), in.f32(), in.f32() };
        Rotator rotation;
        rotation.pitch = static_cast<int32_t>(in.u32());
        rotation.yaw = static_cast<int32_t>(in.u32());
        rotation.roll = static_cast<int32_t>(in.u32());
        uint8_t role = in.u8();
        if (!world) {
            return;
        }
        UObject* actor = nullptr;
        if (existing) {
            actor = objects::findObject(path, nullptr);
        } else {
            UClass* cls = objects::findClass(path);
            objects::flushLoads();
            actor = world->spawn(cls, nullptr, Name(), location, rotation, nullptr, true, nullptr, true);
        }
        if (!actor || !isActor(actor)) {
            return;
        }
        actor::Role(actor) = role;
        actor::RemoteRole(actor) = ROLE_Authority;
        Channel channel;
        channel.id = id;
        channel.actor = actor;
        channel.initial = false;
        connection.byId[id] = actor;
        connection.channels[actor] = std::move(channel);
    }

    void Driver::receiveUpdate(Connection& connection, const uint8_t* data, size_t size)
    {
        In in { data, size };
        uint32_t id = in.u32();
        uint16_t count = in.u16();
        auto found = connection.byId.find(id);
        UObject* actor = found == connection.byId.end() ? nullptr : found->second;
        if (!actor || actor->destroyed) {
            return;
        }
        if (isServer && (!connection.actor || topOwner(actor) != connection.actor)) {
            return;
        }
        static const Name nameRole("Role");
        static const Name nameRemoteRole("RemoteRole");
        static const Name nameLocation("Location");
        static const Name namePhysics("Physics");
        static const Name nameBase("Base");
        static const Name namebCollideActors("bCollideActors");
        static const Name nameCollisionRadius("CollisionRadius");
        static const Name nameCollisionHeight("CollisionHeight");
        static const Name namebBlockActors("bBlockActors");
        static const Name namebBlockPlayers("bBlockPlayers");
        const std::vector<UProperty*>& properties = actor->cls->properties;
        bool collision = false;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t index = in.u16();
            uint8_t element = in.u8();
            if (index >= properties.size()) {
                throw std::runtime_error("bad property index");
            }
            UProperty* property = properties[index];
            if (element >= property->arrayDim) {
                throw std::runtime_error("bad property element");
            }
            uint8_t* address = actor->data + property->offset + static_cast<size_t>(element) * property->elementSize();
            const Name& name = property->name;
            if (!isServer && name == nameRole) {
                address = reinterpret_cast<uint8_t*>(&actor::RemoteRole(actor));
            } else if (!isServer && name == nameRemoteRole) {
                address = reinterpret_cast<uint8_t*>(&actor::Role(actor));
            }
            if (name == nameLocation) {
                Vector location;
                readValue(in, connection, property, reinterpret_cast<uint8_t*>(&location));
                if (!(location == actor::Location(actor))) {
                    world->farMoveActor(actor, location, false, true);
                }
            } else if (name == namePhysics) {
                uint8_t physics = 0;
                readValue(in, connection, property, &physics);
                setPhysics(actor, physics, nullptr);
            } else if (name == nameBase) {
                UObject* base = nullptr;
                readValue(in, connection, property, reinterpret_cast<uint8_t*>(&base));
                setBase(actor, base, true);
            } else {
                readValue(in, connection, property, address);
                collision = collision || name == namebCollideActors || name == nameCollisionRadius || name == nameCollisionHeight || name == namebBlockActors || name == namebBlockPlayers;
            }
        }
        if (collision) {
            world->collisionHash.update(actor);
        }
    }

    void Driver::receiveClose(Connection& connection, const uint8_t* data, size_t size)
    {
        In in { data, size };
        uint32_t id = in.u32();
        bool keep = in.u8() != 0;
        auto found = connection.byId.find(id);
        if (found == connection.byId.end()) {
            return;
        }
        UObject* actor = found->second;
        connection.byId.erase(found);
        connection.channels.erase(actor);
        if (!keep && world && actor && !actor->destroyed && !actor::bStatic(actor) && !actor::bNoDelete(actor)) {
            world->destroy(actor, true);
        }
    }

    void Driver::receiveCall(Connection& connection, const uint8_t* data, size_t size)
    {
        In in { data, size };
        uint32_t id = in.u32();
        Name functionName(in.str());
        auto found = connection.byId.find(id);
        UObject* actor = found == connection.byId.end() ? nullptr : found->second;
        if (!actor || actor->destroyed) {
            return;
        }
        if (isServer && (!connection.actor || topOwner(actor) != connection.actor)) {
            return;
        }
        UFunction* function = vm::findFunction(actor, functionName);
        if (!function || !(function->functionFlags & FUNC_Net)) {
            return;
        }
        vm::Call call(actor, function);
        if (!call.valid()) {
            return;
        }
        for (size_t i = 0; i < function->params.size(); i++) {
            UProperty* parameter = function->params[i];
            for (int32_t element = 0; element < parameter->arrayDim; element++) {
                readValue(in, connection, parameter, call.parameter(i) + static_cast<size_t>(element) * parameter->elementSize());
            }
        }
        call.run();
    }

    void Driver::receivePossess(Connection& connection, const uint8_t* data, size_t size)
    {
        In in { data, size };
        auto found = connection.byId.find(in.u32());
        if (found == connection.byId.end() || !world) {
            return;
        }
        connection.actor = found->second;
        world->possessLocal(connection.actor);
        setNetSpeed(world->viewport);
    }

    bool Driver::remoteCall(UObject* object, UFunction* function, uint8_t* parameters)
    {
        if (!world || !isActor(object) || object->destroyed) {
            return false;
        }
        uint32_t flags = function->functionFlags;
        if (flags & FUNC_Static) {
            return false;
        }
        bool absorb = actor::Role(object) <= ROLE_SimulatedProxy && !(flags & (FUNC_Simulated | FUNC_Native));
        if (!(flags & FUNC_Net)) {
            return absorb;
        }
        Connection* connection = nullptr;
        if (isServer) {
            UObject* top = topOwner(object);
            connection = top && isPlayerPawn(top) ? connectionOf(top) : nullptr;
        } else if (!connections.empty()) {
            connection = connections.front().get();
        }
        if (!connection) {
            return absorb;
        }
        UFunction* root = function;
        while (UFunction* super = cast<UFunction>(root->superField)) {
            root = super;
        }
        if (!condition(object, root, root->replicationOffset)) {
            return absorb;
        }
        auto it = connection->channels.find(object);
        if (it == connection->channels.end()) {
            return true;
        }
        Out body;
        body.u32(it->second.id);
        body.str(root->name.str());
        for (size_t i = 0; i < root->params.size() && i < function->params.size(); i++) {
            UProperty* parameter = function->params[i];
            for (int32_t element = 0; element < parameter->arrayDim; element++) {
                bool resolved = true;
                writeValue(body, *connection, parameter, parameters + parameter->offset + static_cast<size_t>(element) * parameter->elementSize(), resolved);
            }
        }
        message(*connection, MessageCall, body);
        return true;
    }
}
