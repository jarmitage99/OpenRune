#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/net_socket.h"
#include "engine/world.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace rune {
    namespace {
        constexpr uint8_t ModeText = 0;
        constexpr uint8_t ModeLine = 1;
        constexpr uint8_t ModeBinary = 2;
        constexpr uint8_t ReceiveEvent = 1;

        constexpr uint8_t StateInitialized = 0;
        constexpr uint8_t StateReady = 1;
        constexpr uint8_t StateListening = 2;
        constexpr uint8_t StateConnecting = 3;
        constexpr uint8_t StateConnected = 4;
        constexpr uint8_t StateConnectClosePending = 6;
        constexpr uint8_t StateListenClosing = 7;

        struct IpAddr {
            int32_t addr = 0;
            int32_t port = 0;
        };

        struct Link {
            net::Socket socket;
            bool udp = false;
            uint16_t port = 0;
            std::vector<uint8_t> pending;
            std::vector<uint8_t> outgoing;
            bool resolved = false;
            bool resolveFailed = false;
            uint32_t resolvedIp = 0;
        };

        std::unordered_map<UObject*, Link>& links()
        {
            static std::unordered_map<UObject*, Link> table;
            return table;
        }

        template <typename T>
        T& field(UObject* object, const Name& name)
        {
            return *reinterpret_cast<T*>(object->propertyAddress(name));
        }

        uint8_t& linkState(UObject* self)
        {
            static const Name nameLinkState("LinkState");
            return field<uint8_t>(self, nameLinkState);
        }

        void setDataPending(UObject* self, const Link& link)
        {
            static const Name nameDataPending("DataPending");
            field<int32_t>(self, nameDataPending) = link.pending.empty() ? 0 : 1;
        }

        net::Address toAddress(const IpAddr& value)
        {
            return { static_cast<uint32_t>(value.addr), static_cast<uint16_t>(value.port) };
        }

        IpAddr fromAddress(const net::Address& value)
        {
            return { static_cast<int32_t>(value.ip), static_cast<int32_t>(value.port) };
        }

        bool bindLink(Link& link, int32_t port, bool next)
        {
            for (int32_t attempt = 0; attempt < (next ? 20 : 1); attempt++) {
                if (!link.socket.open(link.udp ? net::Socket::Kind::Udp : net::Socket::Kind::Tcp)) {
                    return false;
                }
                if (link.udp) {
                    link.socket.broadcast();
                }
                if (link.socket.bind(static_cast<uint16_t>(port + attempt))) {
                    link.port = link.socket.localPort();
                    return true;
                }
            }
            link.socket.close();
            return false;
        }

        std::string takeText(Link& link, size_t count)
        {
            std::string text(link.pending.begin(), link.pending.begin() + static_cast<ptrdiff_t>(count));
            link.pending.erase(link.pending.begin(), link.pending.begin() + static_cast<ptrdiff_t>(count));
            return text;
        }

        char validationChar(uint8_t value)
        {
            if (value < 26) {
                return static_cast<char>(value + 'A');
            }
            if (value < 52) {
                return static_cast<char>(value + 71);
            }
            if (value < 62) {
                return static_cast<char>(value - 4);
            }
            if (value == 62) {
                return '+';
            }
            return value == 63 ? '/' : '\0';
        }

        std::string validate(const std::string& challenge, const std::string& gameName)
        {
            std::string key;
            if (iequals(gameName, "rune")) {
                key = "BnA4a3";
            } else if (iequals(gameName, "unreal")) {
                key = "DAncRK";
            } else if (iequals(gameName, "ut")) {
                key = "Z5Nfb0";
            } else {
                return {};
            }
            uint8_t table[256];
            for (int i = 0; i < 256; i++) {
                table[i] = static_cast<uint8_t>(i);
            }
            uint8_t a = 0;
            for (size_t i = 0; i < 256; i++) {
                a = static_cast<uint8_t>(a + table[i] + static_cast<uint8_t>(key[i % key.size()]));
                std::swap(table[a], table[i]);
            }
            std::vector<uint8_t> mixed;
            a = 0;
            uint8_t b = 0;
            for (char c : challenge) {
                a = static_cast<uint8_t>(a + static_cast<uint8_t>(c) + 1);
                uint8_t x = table[a];
                b = static_cast<uint8_t>(b + x);
                uint8_t y = table[b];
                table[b] = x;
                table[a] = y;
                mixed.push_back(static_cast<uint8_t>(static_cast<uint8_t>(c) ^ table[static_cast<uint8_t>(x + y)]));
            }
            while (mixed.size() % 3) {
                mixed.push_back(0);
            }
            std::string result;
            for (size_t i = 0; i < mixed.size(); i += 3) {
                uint8_t x = mixed[i];
                uint8_t y = mixed[i + 1];
                uint8_t z = mixed[i + 2];
                result += validationChar(static_cast<uint8_t>(x >> 2));
                result += validationChar(static_cast<uint8_t>(((x & 3) << 4) | (y >> 4)));
                result += validationChar(static_cast<uint8_t>(((y & 15) << 2) | (z >> 6)));
                result += validationChar(static_cast<uint8_t>(z & 63));
            }
            return result;
        }

        void nativeIsDataPending(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            auto it = links().find(self);
            vm::retBool(result, it != links().end() && !it->second.pending.empty());
        }

        void nativeParseURL(UObject*, Frame& stack, void* result)
        {
            std::string url = vm::get<std::string>(stack);
            std::string* addr = vm::getRef<std::string>(stack);
            int32_t* port = vm::getRef<int32_t>(stack);
            std::string* levelName = vm::getRef<std::string>(stack);
            std::string* entryName = vm::getRef<std::string>(stack);
            vm::finish(stack);
            std::string rest = url;
            size_t scheme = rest.find("://");
            if (scheme != std::string::npos) {
                rest = rest.substr(scheme + 3);
            }
            size_t slash = rest.find('/');
            std::string host = rest.substr(0, slash);
            std::string path = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
            *port = 7777;
            size_t colon = host.find(':');
            if (colon != std::string::npos) {
                *port = std::atoi(host.substr(colon + 1).c_str());
                host = host.substr(0, colon);
            }
            *addr = host;
            size_t hash = path.find('#');
            *levelName = path.substr(0, hash);
            *entryName = hash == std::string::npos ? std::string() : path.substr(hash + 1);
            vm::retBool(result, !host.empty());
        }

        void nativeResolve(UObject* self, Frame& stack, void*)
        {
            std::string domain = vm::get<std::string>(stack);
            vm::finish(stack);
            static const Name nameUdpLink("UdpLink");
            Link& link = links()[self];
            link.udp = self->isA(nameUdpLink);
            uint32_t ip = 0;
            link.resolved = net::resolve(domain, ip);
            link.resolveFailed = !link.resolved;
            link.resolvedIp = ip;
        }

        void nativeGetLastError(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            vm::ret<int32_t>(result, 0);
        }

        void nativeIpAddrToString(UObject*, Frame& stack, void* result)
        {
            IpAddr address = vm::get<IpAddr>(stack);
            vm::finish(stack);
            vm::ret<std::string>(result, net::ipText(static_cast<uint32_t>(address.addr)) + ":" + std::to_string(address.port));
        }

        void nativeStringToIpAddr(UObject*, Frame& stack, void* result)
        {
            std::string text = vm::get<std::string>(stack);
            IpAddr* address = vm::getRef<IpAddr>(stack);
            vm::finish(stack);
            std::string host = text.substr(0, text.find(':'));
            uint32_t parts[4] = {};
            int count = std::sscanf(host.c_str(), "%u.%u.%u.%u", &parts[0], &parts[1], &parts[2], &parts[3]);
            if (count != 4) {
                vm::retBool(result, false);
                return;
            }
            address->addr = static_cast<int32_t>(parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3]);
            if (text.find(':') != std::string::npos) {
                address->port = std::atoi(text.substr(text.find(':') + 1).c_str());
            }
            vm::retBool(result, true);
        }

        void nativeValidate(UObject*, Frame& stack, void* result)
        {
            std::string challenge = vm::get<std::string>(stack);
            std::string gameName = vm::get<std::string>(stack);
            vm::finish(stack);
            vm::ret<std::string>(result, validate(challenge, gameName));
        }

        void nativeGetLocalIP(UObject*, Frame& stack, void*)
        {
            IpAddr* address = vm::getRef<IpAddr>(stack);
            vm::finish(stack);
            address->addr = static_cast<int32_t>(net::localIp());
        }

        void nativeTcpBindPort(UObject* self, Frame& stack, void* result)
        {
            int32_t port = vm::getOpt<int32_t>(stack, 0);
            bool next = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            Link& link = links()[self];
            link.udp = false;
            if (!bindLink(link, port, next)) {
                vm::ret<int32_t>(result, 0);
                return;
            }
            field<int32_t>(self, "Port") = link.port;
            linkState(self) = StateReady;
            vm::ret<int32_t>(result, link.port);
        }

        void nativeTcpListen(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            auto it = links().find(self);
            if (it != links().end() && it->second.socket.valid() && linkState(self) == StateListenClosing) {
                linkState(self) = StateListening;
                vm::retBool(result, true);
                return;
            }
            if (it == links().end() || !it->second.socket.valid() || !it->second.socket.listen()) {
                vm::retBool(result, false);
                return;
            }
            linkState(self) = StateListening;
            vm::retBool(result, true);
        }

        void nativeTcpOpen(UObject* self, Frame& stack, void* result)
        {
            IpAddr address = vm::get<IpAddr>(stack);
            vm::finish(stack);
            Link& link = links()[self];
            if (!link.socket.valid() && !bindLink(link, 0, false)) {
                vm::retBool(result, false);
                return;
            }
            bool started = link.socket.connect(toAddress(address));
            if (started) {
                linkState(self) = StateConnecting;
                field<IpAddr>(self, "RemoteAddr") = address;
            }
            vm::retBool(result, started);
        }

        void nativeTcpClose(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            auto it = links().find(self);
            if (it == links().end()) {
                vm::retBool(result, false);
                return;
            }
            uint8_t state = linkState(self);
            if (state == StateListening) {
                linkState(self) = StateListenClosing;
            } else if (state == StateConnected || state == StateConnecting) {
                linkState(self) = StateConnectClosePending;
            }
            vm::retBool(result, true);
        }

        void nativeTcpIsConnected(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            vm::retBool(result, linkState(self) == StateConnected);
        }

        int32_t queueSend(UObject* self, const std::string& data)
        {
            auto it = links().find(self);
            if (it == links().end() || linkState(self) != StateConnected) {
                return 0;
            }
            Link& link = it->second;
            link.outgoing.insert(link.outgoing.end(), data.begin(), data.end());
            int64_t sent = link.socket.send(link.outgoing.data(), link.outgoing.size());
            link.outgoing.erase(link.outgoing.begin(), link.outgoing.begin() + static_cast<ptrdiff_t>(sent));
            return static_cast<int32_t>(data.size());
        }

        void nativeTcpSendText(UObject* self, Frame& stack, void* result)
        {
            std::string text = vm::get<std::string>(stack);
            vm::finish(stack);
            if (field<uint8_t>(self, "LinkMode") == ModeLine) {
                text += "\r\n";
            }
            vm::ret<int32_t>(result, queueSend(self, text));
        }

        void nativeTcpSendBinary(UObject* self, Frame& stack, void* result)
        {
            int32_t count = vm::get<int32_t>(stack);
            uint8_t* bytes = vm::getRef<uint8_t>(stack);
            vm::finish(stack);
            count = std::clamp(count, 0, 255);
            vm::ret<int32_t>(result, queueSend(self, std::string(reinterpret_cast<const char*>(bytes), static_cast<size_t>(count))));
        }

        void nativeTcpReadText(UObject* self, Frame& stack, void* result)
        {
            std::string* text = vm::getRef<std::string>(stack);
            vm::finish(stack);
            auto it = links().find(self);
            if (it == links().end() || it->second.pending.empty()) {
                text->clear();
                vm::ret<int32_t>(result, 0);
                return;
            }
            *text = takeText(it->second, it->second.pending.size());
            setDataPending(self, it->second);
            vm::ret<int32_t>(result, static_cast<int32_t>(text->size()));
        }

        void nativeTcpReadBinary(UObject* self, Frame& stack, void* result)
        {
            int32_t count = vm::get<int32_t>(stack);
            uint8_t* bytes = vm::getRef<uint8_t>(stack);
            vm::finish(stack);
            auto it = links().find(self);
            if (it == links().end()) {
                vm::ret<int32_t>(result, 0);
                return;
            }
            size_t taken = std::min(it->second.pending.size(), static_cast<size_t>(std::clamp(count, 0, 255)));
            std::string data = takeText(it->second, taken);
            std::memcpy(bytes, data.data(), taken);
            setDataPending(self, it->second);
            vm::ret<int32_t>(result, static_cast<int32_t>(taken));
        }

        void nativeUdpBindPort(UObject* self, Frame& stack, void* result)
        {
            int32_t port = vm::getOpt<int32_t>(stack, 0);
            bool next = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            Link& link = links()[self];
            link.udp = true;
            if (!bindLink(link, port, next)) {
                vm::ret<int32_t>(result, 0);
                return;
            }
            field<int32_t>(self, "Port") = link.port;
            vm::ret<int32_t>(result, link.port);
        }

        bool udpSend(UObject* self, const IpAddr& address, const std::string& data)
        {
            auto it = links().find(self);
            if (it == links().end() || !it->second.socket.valid()) {
                return false;
            }
            return it->second.socket.sendTo(data.data(), data.size(), toAddress(address)) == static_cast<int64_t>(data.size());
        }

        void nativeUdpSendText(UObject* self, Frame& stack, void* result)
        {
            IpAddr address = vm::get<IpAddr>(stack);
            std::string text = vm::get<std::string>(stack);
            vm::finish(stack);
            if (field<uint8_t>(self, "LinkMode") == ModeLine) {
                text += "\r\n";
            }
            vm::retBool(result, udpSend(self, address, text));
        }

        void nativeUdpSendBinary(UObject* self, Frame& stack, void* result)
        {
            IpAddr address = vm::get<IpAddr>(stack);
            int32_t count = vm::get<int32_t>(stack);
            uint8_t* bytes = vm::getRef<uint8_t>(stack);
            vm::finish(stack);
            count = std::clamp(count, 0, 255);
            vm::retBool(result, udpSend(self, address, std::string(reinterpret_cast<const char*>(bytes), static_cast<size_t>(count))));
        }

        int64_t udpReceive(UObject* self, uint8_t* buffer, size_t size, IpAddr& from)
        {
            auto it = links().find(self);
            if (it == links().end() || !it->second.socket.valid()) {
                return -1;
            }
            net::Address address;
            int64_t received = it->second.socket.receiveFrom(buffer, size, address);
            if (received >= 0) {
                from = fromAddress(address);
            }
            return received;
        }

        void nativeUdpReadText(UObject* self, Frame& stack, void* result)
        {
            IpAddr* address = vm::getRef<IpAddr>(stack);
            std::string* text = vm::getRef<std::string>(stack);
            vm::finish(stack);
            uint8_t buffer[4096];
            int64_t received = udpReceive(self, buffer, sizeof(buffer), *address);
            *text = received > 0 ? std::string(reinterpret_cast<const char*>(buffer), static_cast<size_t>(received)) : std::string();
            vm::ret<int32_t>(result, received > 0 ? static_cast<int32_t>(received) : 0);
        }

        void nativeUdpReadBinary(UObject* self, Frame& stack, void* result)
        {
            IpAddr* address = vm::getRef<IpAddr>(stack);
            int32_t count = vm::get<int32_t>(stack);
            uint8_t* bytes = vm::getRef<uint8_t>(stack);
            vm::finish(stack);
            uint8_t buffer[4096];
            int64_t received = udpReceive(self, buffer, sizeof(buffer), *address);
            size_t taken = received > 0 ? std::min(static_cast<size_t>(received), static_cast<size_t>(std::clamp(count, 0, 255))) : 0;
            std::memcpy(bytes, buffer, taken);
            vm::ret<int32_t>(result, static_cast<int32_t>(taken));
        }

        void deliverText(UObject* actor, Link& link, uint8_t mode)
        {
            static const Name nameReceivedText("ReceivedText");
            static const Name nameReceivedLine("ReceivedLine");
            static const Name nameReceivedBinary("ReceivedBinary");
            while (!link.pending.empty() && !actor->destroyed) {
                if (mode == ModeLine) {
                    auto end = std::find(link.pending.begin(), link.pending.end(), static_cast<uint8_t>('\n'));
                    if (end == link.pending.end()) {
                        return;
                    }
                    std::string line = takeText(link, static_cast<size_t>(end - link.pending.begin()) + 1);
                    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                        line.pop_back();
                    }
                    vm::Call(actor, nameReceivedLine).s(line).run();
                } else if (mode == ModeBinary) {
                    size_t count = std::min<size_t>(link.pending.size(), 255);
                    std::string data = takeText(link, count);
                    vm::Call call(actor, nameReceivedBinary);
                    if (call.valid()) {
                        call.i(static_cast<int32_t>(count));
                        std::memcpy(call.parameter(1), data.data(), count);
                        call.run();
                    }
                } else {
                    vm::Call(actor, nameReceivedText).s(takeText(link, link.pending.size())).run();
                }
            }
        }

        void tickUdp(UObject* actor, Link& link)
        {
            static const Name nameReceivedText("ReceivedText");
            static const Name nameReceivedLine("ReceivedLine");
            static const Name nameReceivedBinary("ReceivedBinary");
            static const Name nameReceiveMode("ReceiveMode");
            static const Name nameLinkMode("LinkMode");
            if (field<uint8_t>(actor, nameReceiveMode) != ReceiveEvent || !link.socket.valid()) {
                return;
            }
            uint8_t mode = field<uint8_t>(actor, nameLinkMode);
            for (int packets = 0; packets < 64 && !actor->destroyed; packets++) {
                uint8_t buffer[4096];
                net::Address from;
                int64_t received = link.socket.receiveFrom(buffer, sizeof(buffer), from);
                if (received < 0) {
                    return;
                }
                IpAddr address = fromAddress(from);
                std::string data(reinterpret_cast<const char*>(buffer), static_cast<size_t>(received));
                Name event = mode == ModeBinary ? nameReceivedBinary : (mode == ModeLine ? nameReceivedLine : nameReceivedText);
                vm::Call call(actor, event);
                if (!call.valid()) {
                    continue;
                }
                *reinterpret_cast<IpAddr*>(call.parameter(0)) = address;
                if (mode == ModeBinary) {
                    size_t count = std::min<size_t>(data.size(), 255);
                    *reinterpret_cast<int32_t*>(call.parameter(1)) = static_cast<int32_t>(count);
                    std::memcpy(call.parameter(2), data.data(), count);
                } else {
                    while (mode == ModeLine && !data.empty() && (data.back() == '\n' || data.back() == '\r')) {
                        data.pop_back();
                    }
                    *reinterpret_cast<std::string*>(call.parameter(1)) = data;
                }
                call.run();
            }
        }

        void tickTcp(World& world, UObject* actor, Link& link)
        {
            static const Name nameAccepted("Accepted");
            static const Name nameOpened("Opened");
            static const Name nameClosed("Closed");
            static const Name nameReceiveMode("ReceiveMode");
            static const Name nameLinkMode("LinkMode");
            uint8_t state = linkState(actor);
            if (state == StateListening) {
                net::Socket accepted;
                net::Address from;
                while (link.socket.accept(accepted, from) && !actor->destroyed) {
                    UClass* acceptClass = field<UClass*>(actor, "AcceptClass");
                    UObject* target = actor;
                    if (acceptClass) {
                        target = world.spawn(acceptClass, actor, Name(), actor::Location(actor), {}, nullptr, true);
                        if (!target) {
                            continue;
                        }
                    } else {
                        link.socket.close();
                    }
                    Link& child = links()[target];
                    child.udp = false;
                    child.socket = std::move(accepted);
                    linkState(target) = StateConnected;
                    field<IpAddr>(target, "RemoteAddr") = fromAddress(from);
                    vm::event(target, nameAccepted);
                    if (!acceptClass) {
                        return;
                    }
                }
                return;
            }
            if (state == StateConnecting) {
                if (link.socket.connected()) {
                    linkState(actor) = StateConnected;
                    vm::event(actor, nameOpened);
                } else if (link.socket.failed()) {
                    link.socket.close();
                    linkState(actor) = StateInitialized;
                }
                return;
            }
            if (state == StateConnectClosePending) {
                if (!link.outgoing.empty()) {
                    int64_t sent = link.socket.send(link.outgoing.data(), link.outgoing.size());
                    link.outgoing.erase(link.outgoing.begin(), link.outgoing.begin() + static_cast<ptrdiff_t>(std::max<int64_t>(sent, 0)));
                }
                if (link.outgoing.empty() || link.socket.failed()) {
                    link.socket.close();
                    linkState(actor) = StateInitialized;
                    vm::event(actor, nameClosed);
                }
                return;
            }
            if (state != StateConnected) {
                return;
            }
            if (!link.outgoing.empty()) {
                int64_t sent = link.socket.send(link.outgoing.data(), link.outgoing.size());
                link.outgoing.erase(link.outgoing.begin(), link.outgoing.begin() + static_cast<ptrdiff_t>(sent));
            }
            uint8_t buffer[8192];
            for (;;) {
                int64_t received = link.socket.receive(buffer, sizeof(buffer));
                if (received <= 0) {
                    break;
                }
                link.pending.insert(link.pending.end(), buffer, buffer + received);
            }
            setDataPending(actor, link);
            if (field<uint8_t>(actor, nameReceiveMode) == ReceiveEvent) {
                deliverText(actor, link, field<uint8_t>(actor, nameLinkMode));
                if (actor->destroyed) {
                    return;
                }
                setDataPending(actor, link);
            }
            if (link.socket.failed() && link.pending.empty()) {
                link.socket.close();
                linkState(actor) = StateInitialized;
                vm::event(actor, nameClosed);
            }
        }
    }

    namespace ipdrv {
        void tick(World& world)
        {
            static const Name nameResolved("Resolved");
            static const Name nameResolveFailed("ResolveFailed");
            std::vector<UObject*> actors;
            for (auto& [actor, link] : links()) {
                actors.push_back(actor);
            }
            for (UObject* actor : actors) {
                auto it = links().find(actor);
                if (it == links().end() || actor->destroyed || World::of(actor) != &world) {
                    continue;
                }
                Link& link = it->second;
                if (link.resolved) {
                    link.resolved = false;
                    vm::Call call(actor, nameResolved);
                    if (call.valid()) {
                        *reinterpret_cast<IpAddr*>(call.parameter(0)) = { static_cast<int32_t>(link.resolvedIp), 0 };
                        call.run();
                    }
                    continue;
                }
                if (link.resolveFailed) {
                    link.resolveFailed = false;
                    vm::event(actor, nameResolveFailed);
                    continue;
                }
                if (link.udp) {
                    tickUdp(actor, link);
                } else {
                    tickTcp(world, actor, link);
                }
            }
        }

        void actorDestroyed(UObject* actor)
        {
            links().erase(actor);
        }

        void actorSpawned(UObject* actor)
        {
            static const Name nameInternetLink("InternetLink");
            if (actor->isA(nameInternetLink)) {
                field<uint8_t>(actor, "LinkMode") = ModeText;
                field<uint8_t>(actor, "ReceiveMode") = ReceiveEvent;
            }
        }

        void reset()
        {
            links().clear();
        }
    }

    void registerIpDrvNatives()
    {
        vm::registerNative("InternetLink", "IsDataPending", nativeIsDataPending);
        vm::registerNative("InternetLink", "ParseURL", nativeParseURL);
        vm::registerNative("InternetLink", "Resolve", nativeResolve);
        vm::registerNative("InternetLink", "GetLastError", nativeGetLastError);
        vm::registerNative("InternetLink", "IpAddrToString", nativeIpAddrToString);
        vm::registerNative("InternetLink", "StringToIpAddr", nativeStringToIpAddr);
        vm::registerNative("InternetLink", "Validate", nativeValidate);
        vm::registerNative("InternetLink", "GetLocalIP", nativeGetLocalIP);
        vm::registerNative("TcpLink", "BindPort", nativeTcpBindPort);
        vm::registerNative("TcpLink", "Listen", nativeTcpListen);
        vm::registerNative("TcpLink", "Open", nativeTcpOpen);
        vm::registerNative("TcpLink", "Close", nativeTcpClose);
        vm::registerNative("TcpLink", "IsConnected", nativeTcpIsConnected);
        vm::registerNative("TcpLink", "SendText", nativeTcpSendText);
        vm::registerNative("TcpLink", "SendBinary", nativeTcpSendBinary);
        vm::registerNative("TcpLink", "ReadText", nativeTcpReadText);
        vm::registerNative("TcpLink", "ReadBinary", nativeTcpReadBinary);
        vm::registerNative("UdpLink", "BindPort", nativeUdpBindPort);
        vm::registerNative("UdpLink", "SendText", nativeUdpSendText);
        vm::registerNative("UdpLink", "SendBinary", nativeUdpSendBinary);
        vm::registerNative("UdpLink", "ReadText", nativeUdpReadText);
        vm::registerNative("UdpLink", "ReadBinary", nativeUdpReadBinary);
    }
}
