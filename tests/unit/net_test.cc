#include "audio/audio.h"
#include "core/config.h"
#include "core/url.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/net.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
    template <typename Step, typename Done>
    bool waitFor(Step step, Done done)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            step();
        }
        return true;
    }

    struct PlayerReport {
        char name[32] = {};
        float x = 0;
        float y = 0;
        float z = 0;
    };

    struct ServerReport {
        uint16_t port = 0;
        int32_t players = 0;
        int32_t connections = 0;
        PlayerReport player[4];
        float kegX = 0;
        float kegY = 0;
        float kegZ = 0;
        uint16_t webPort = 0;
        uint16_t queryPort = 0;
        char map[32] = {};
    };

    struct ServerOptions {
        bool services = false;
        bool listen = false;
    };

    constexpr char CommandStop = 1;
    constexpr char CommandTravel = 2;

    const PlayerReport* findPlayer(const ServerReport& report, const std::string& name)
    {
        for (const PlayerReport& player : report.player) {
            if (name == player.name) {
                return &player;
            }
        }
        return nullptr;
    }

    std::unique_ptr<rune::World> startServerWorld(rune::net::Driver& driver, const ServerOptions& options, const std::string& url)
    {
        auto world = std::make_unique<rune::World>();
        world->netMode = options.listen ? rune::net::NM_ListenServer : rune::net::NM_DedicatedServer;
        world->netDriver = &driver;
        if (!world->loadMap(url)) {
            _exit(2);
        }
        driver.attach(world.get());
        return world;
    }

    void runServer(int reportPipe, int stopPipe, ServerOptions options)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        if (options.services) {
            rune::config::system().set("UWeb.WebServer", "bEnabled", "True");
            rune::config::system().set("UWeb.WebServer", "ListenPort", "0");
            rune::UClass* webServer = rune::objects::findClass("UWeb.WebServer");
            rune::objects::flushLoads();
            if (webServer) {
                rune::config::applyObjectConfig(webServer->defaultObject);
            }
        }
        std::string error;
        std::unique_ptr<rune::net::Driver> driver = rune::net::Driver::listen(0, error);
        if (!driver) {
            _exit(2);
        }
        std::unique_ptr<rune::World> world = startServerWorld(*driver, options, options.listen ? "DM-Hel?game=RuneI.RuneMultiPlayer?Name=Host" : "DM-Hel?game=RuneI.RuneMultiPlayer");
        ServerReport report;
        report.port = driver->port();
        if (options.services) {
            world->spawnServerActors({ "UWeb.WebServer", "IpServer.UdpServerQuery", "IpServer.UdpServerUplink DoUplink=True MasterServerAddress=master.invalid" });
            for (rune::UObject* a : world->actors) {
                if (a && !a->destroyed && a->isA(rune::Name("WebServer"))) {
                    report.webPort = static_cast<uint16_t>(*reinterpret_cast<int32_t*>(a->propertyAddress(rune::Name("Port"))));
                }
                if (a && !a->destroyed && a->isA(rune::Name("UdpServerQuery"))) {
                    report.queryPort = static_cast<uint16_t>(*reinterpret_cast<int32_t*>(a->propertyAddress(rune::Name("Port"))));
                }
            }
        }
        if (write(reportPipe, &report, sizeof(report)) != static_cast<ssize_t>(sizeof(report))) {
            _exit(3);
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        rune::UObject* keg = nullptr;
        rune::UObject* sound = rune::objects::loadObject("WeaponsSnd.ImpCrashes.crashxbucket01", nullptr);
        rune::objects::flushLoads();
        int frame = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            char command = 0;
            if (read(stopPipe, &command, 1) == 1) {
                if (command == CommandStop) {
                    break;
                }
                rune::vm::Call(world->levelInfo, rune::Name("ServerTravel")).s("DM-Ruins").b(false).run();
            }
            if (world->travelPending) {
                std::string url = rune::travelUrl(world->url, world->travelUrl, world->travelType);
                driver->attach(nullptr);
                rune::objects::unloadPackage(world->level->outermost()->name);
                world.reset();
                world = startServerWorld(*driver, options, url);
                keg = nullptr;
            }
            if (keg && sound && ++frame % 15 == 0) {
                rune::vm::Call(keg, rune::Name("PlaySound")).obj(sound).run();
            }
            for (rune::UObject* p = rune::levelinfo::PawnList(world->levelInfo); p && !keg; p = rune::pawn::nextPawn(p)) {
                if (rune::isPlayerPawn(p) && rune::playerpawn::Player(p) && rune::playerpawn::Player(p) != world->viewport) {
                    rune::UClass* kegClass = rune::objects::findClass("RuneI.Keg");
                    rune::objects::flushLoads();
                    keg = world->spawn(kegClass, nullptr, rune::Name(), rune::actor::Location(p) + rune::Vector { 0, 0, 100 }, {}, nullptr, true);
                    if (keg) {
                        rune::actor::Physics(keg) = rune::PHYS_None;
                        report.kegX = rune::actor::Location(keg).x;
                        report.kegY = rune::actor::Location(keg).y;
                        report.kegZ = rune::actor::Location(keg).z;
                    }
                }
            }
            for (rune::UObject* p = rune::levelinfo::PawnList(world->levelInfo); p; p = rune::pawn::nextPawn(p)) {
                if (rune::isPlayerPawn(p)) {
                    rune::actor::bAlwaysRelevant(p) = 1;
                }
            }
            world->tick(1.0f / 30.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        for (rune::UObject* p = rune::levelinfo::PawnList(world->levelInfo); p; p = rune::pawn::nextPawn(p)) {
            if (rune::isPlayerPawn(p) && rune::playerpawn::Player(p) && report.players < 4) {
                PlayerReport& player = report.player[report.players++];
                player.x = rune::actor::Location(p).x;
                player.y = rune::actor::Location(p).y;
                player.z = rune::actor::Location(p).z;
                rune::UObject* info = rune::pawn::PlayerReplicationInfo(p);
                if (info) {
                    std::snprintf(player.name, sizeof(player.name), "%s", rune::playerreplicationinfo::PlayerName(info).c_str());
                }
            }
        }
        rune::UClass* connectionClass = rune::objects::intrinsicClass(rune::Name("NetConnection"));
        for (rune::UObject* object : rune::objects::all()) {
            if (object && !object->destroyed && object->cls == connectionClass && object != connectionClass->defaultObject) {
                report.connections++;
            }
        }
        std::snprintf(report.map, sizeof(report.map), "%s", world->level->outermost()->name.c_str());
        if (write(reportPipe, &report, sizeof(report)) != static_cast<ssize_t>(sizeof(report))) {
            _exit(4);
        }
        _exit(0);
    }

    struct ServerProcess {
        pid_t pid = -1;
        int reports[2] = { -1, -1 };
        int stops[2] = { -1, -1 };
        ServerReport started;

        explicit ServerProcess(ServerOptions options = {})
        {
            rune::objects::unloadPackage(rune::Name("DM-Hel"));
            REQUIRE(pipe(reports) == 0);
            REQUIRE(pipe(stops) == 0);
            pid = fork();
            REQUIRE(pid >= 0);
            if (pid == 0) {
                close(reports[0]);
                close(stops[1]);
                fcntl(stops[0], F_SETFL, O_NONBLOCK);
                runServer(reports[1], stops[0], options);
            }
            close(reports[1]);
            close(stops[0]);
            REQUIRE(read(reports[0], &started, sizeof(started)) == static_cast<ssize_t>(sizeof(started)));
            REQUIRE(started.port != 0);
        }

        void travel()
        {
            char command = CommandTravel;
            REQUIRE(write(stops[1], &command, 1) == 1);
        }

        ServerReport stop()
        {
            char command = CommandStop;
            REQUIRE(write(stops[1], &command, 1) == 1);
            ServerReport report;
            REQUIRE(read(reports[0], &report, sizeof(report)) == static_cast<ssize_t>(sizeof(report)));
            int status = 0;
            waitpid(pid, &status, 0);
            pid = -1;
            return report;
        }

        ~ServerProcess()
        {
            if (pid > 0) {
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
            }
            close(reports[0]);
            close(stops[1]);
            rune::objects::unloadPackage(rune::Name("DM-Hel"));
            rune::objects::unloadPackage(rune::Name("DM-Ruins"));
        }
    };
}

TEST_CASE("a client joins a dedicated server, is possessed as an autonomous proxy and moves on the server", "[data][net]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    ServerProcess server;
    std::string error;
    std::unique_ptr<rune::net::Driver> driver = rune::net::Driver::connect("127.0.0.1", server.started.port, "?Name=NetTester?Class=RuneI.Ragnar", error);
    REQUIRE(driver != nullptr);
    std::string map;
    REQUIRE(driver->waitForWelcome(20.0, map, error));
    CHECK(map == "DM-Hel");
    rune::World world;
    world.netMode = rune::net::NM_Client;
    world.netDriver = driver.get();
    driver->attach(&world);
    REQUIRE(world.loadMap(map));
    CHECK(world.gameInfo == nullptr);
    CHECK(rune::levelinfo::NetMode(world.levelInfo) == rune::net::NM_Client);
    driver->join();
    bool moving = false;
    world.inputHook = [&moving](rune::UObject* pawn, float delta) {
        if (moving && delta >= 0) {
            rune::playerpawn::aForward(pawn) = 600.0f;
            rune::playerpawn::aBaseY(pawn) = 1.0f;
        }
    };
    waitFor([&]() {
        world.tick(1.0f / 30.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); }, [&]() { return world.player() != nullptr; });
    rune::UObject* pawn = world.player();
    REQUIRE(pawn != nullptr);
    CHECK(rune::actor::Role(pawn) == rune::net::ROLE_AutonomousProxy);
    CHECK(rune::actor::RemoteRole(pawn) == rune::net::ROLE_Authority);
    for (int i = 0; i < 60; i++) {
        world.tick(1.0f / 30.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    rune::UObject* info = rune::pawn::PlayerReplicationInfo(pawn);
    REQUIRE(info != nullptr);
    CHECK(rune::playerreplicationinfo::PlayerName(info) == "NetTester");
    rune::Vector start = rune::actor::Location(pawn);
    moving = true;
    for (int i = 0; i < 90; i++) {
        world.tick(1.0f / 30.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    moving = false;
    for (int i = 0; i < 30; i++) {
        world.tick(1.0f / 30.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    rune::Vector end = rune::actor::Location(pawn);
    CHECK((end - start).size() > 50.0f);
    ServerReport report = server.stop();
    CHECK(report.players == 1);
    const PlayerReport* tester = findPlayer(report, "NetTester");
    REQUIRE(tester != nullptr);
    rune::Vector serverLocation { tester->x, tester->y, tester->z };
    CHECK((serverLocation - end).size() < 30.0f);
    CHECK_FALSE(driver->disconnected());
    waitFor([&]() {
        world.tick(1.0f / 30.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); }, [&]() { return driver->disconnected(); });
    CHECK(driver->disconnected());
}
#endif

#ifndef _WIN32
namespace {
    struct Client {
        std::unique_ptr<rune::net::Driver> driver;
        std::unique_ptr<rune::World> world;
        std::string ignoredTravel;
        int32_t ignoredTravelType = -1;

        explicit Client(uint16_t port)
        {
            std::string error;
            driver = rune::net::Driver::connect("127.0.0.1", port, "?Name=NetTester?Class=RuneI.Ragnar", error);
            REQUIRE(driver != nullptr);
            std::string map;
            REQUIRE(driver->waitForWelcome(20.0, map, error));
            enter(map);
        }

        ~Client()
        {
            world->netDriver = nullptr;
            driver.reset();
        }

        void enter(const std::string& map)
        {
            if (world) {
                driver->attach(nullptr);
                rune::objects::unloadPackage(world->level->outermost()->name);
                world.reset();
            }
            world = std::make_unique<rune::World>();
            world->netMode = rune::net::NM_Client;
            world->netDriver = driver.get();
            driver->attach(world.get());
            REQUIRE(world->loadMap(map));
            driver->join();
            waitFor([&]() {
                world->tick(1.0f / 30.0f);
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); }, [&]() { return world->player() != nullptr; });
            REQUIRE(world->player() != nullptr);
        }

        void tick(int count = 1)
        {
            for (int i = 0; i < count; i++) {
                world->tick(1.0f / 30.0f);
                if (world->travelPending) {
                    ignoredTravel = world->travelUrl;
                    ignoredTravelType = world->travelType;
                    world->travelPending = false;
                }
                std::string map;
                if (driver->takeWelcome(map)) {
                    enter(map);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        std::string map() const
        {
            return world->level->outermost()->name.str();
        }

        rune::UObject* remotePlayer(const std::string& name) const
        {
            for (rune::UObject* a : world->actors) {
                if (!a || a->destroyed || !rune::isPlayerPawn(a) || a == world->player()) {
                    continue;
                }
                rune::UObject* info = rune::pawn::PlayerReplicationInfo(a);
                if (info && !info->destroyed && rune::playerreplicationinfo::PlayerName(info) == name) {
                    return a;
                }
            }
            return nullptr;
        }

        bool hasPlayerInfo(const std::string& name) const
        {
            for (rune::UObject* a : world->actors) {
                if (a && !a->destroyed && a->isA(rune::Name("PlayerReplicationInfo")) && rune::playerreplicationinfo::PlayerName(a) == name) {
                    return true;
                }
            }
            return false;
        }
    };

    constexpr char CommandWalk = 3;

    void runPeer(int reportPipe, int commandPipe, uint16_t port)
    {
        std::string error;
        std::unique_ptr<rune::net::Driver> driver = rune::net::Driver::connect("127.0.0.1", port, "?Name=Second?Class=RuneI.Ragnar", error);
        std::string map;
        if (!driver || !driver->waitForWelcome(20.0, map, error)) {
            _exit(2);
        }
        rune::World world;
        world.netMode = rune::net::NM_Client;
        world.netDriver = driver.get();
        driver->attach(&world);
        if (!world.loadMap(map)) {
            _exit(3);
        }
        driver->join();
        int walking = 0;
        world.inputHook = [&walking](rune::UObject* pawn, float delta) {
            if (walking > 0 && delta >= 0) {
                rune::playerpawn::aForward(pawn) = 600.0f;
                rune::playerpawn::aBaseY(pawn) = 1.0f;
            }
        };
        bool ready = false;
        int settle = -1;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline) {
            char command = 0;
            if (read(commandPipe, &command, 1) == 1) {
                if (command == CommandStop) {
                    world.netDriver = nullptr;
                    driver.reset();
                    _exit(0);
                }
                walking = 12;
            }
            world.tick(1.0f / 30.0f);
            if (!ready && world.player() && rune::pawn::PlayerReplicationInfo(world.player())) {
                ready = true;
                char signal = 1;
                if (write(reportPipe, &signal, 1) != 1) {
                    _exit(4);
                }
            }
            if (walking > 0 && --walking == 0) {
                settle = 30;
            }
            if (settle > 0 && --settle == 0) {
                rune::Vector location = rune::actor::Location(world.player());
                if (write(reportPipe, &location, sizeof(location)) != static_cast<ssize_t>(sizeof(location))) {
                    _exit(5);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        _exit(6);
    }

    struct PeerProcess {
        pid_t pid = -1;
        int reports[2] = { -1, -1 };
        int commands[2] = { -1, -1 };

        explicit PeerProcess(uint16_t port)
        {
            REQUIRE(pipe(reports) == 0);
            REQUIRE(pipe(commands) == 0);
            pid = fork();
            REQUIRE(pid >= 0);
            if (pid == 0) {
                close(reports[0]);
                close(commands[1]);
                fcntl(commands[0], F_SETFL, O_NONBLOCK);
                runPeer(reports[1], commands[0], port);
            }
            close(reports[1]);
            close(commands[0]);
            char signal = 0;
            REQUIRE(read(reports[0], &signal, 1) == 1);
        }

        rune::Vector walk()
        {
            char command = CommandWalk;
            REQUIRE(write(commands[1], &command, 1) == 1);
            rune::Vector location;
            REQUIRE(read(reports[0], &location, sizeof(location)) == static_cast<ssize_t>(sizeof(location)));
            return location;
        }

        void leave()
        {
            char command = CommandStop;
            REQUIRE(write(commands[1], &command, 1) == 1);
            int status = -1;
            waitpid(pid, &status, 0);
            pid = -1;
            CHECK(status == 0);
        }

        void drop()
        {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            pid = -1;
        }

        ~PeerProcess()
        {
            if (pid > 0) {
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
            }
            close(reports[0]);
            close(commands[1]);
        }
    };
}

TEST_CASE("server actors replicate to the client as proxies, their sounds are heard and chat comes back as a team message", "[data][net]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    ServerProcess server;
    Client client(server.started.port);
    client.tick(30);
    rune::UObject* keg = nullptr;
    for (rune::UObject* a : client.world->actors) {
        if (a && !a->destroyed && a->cls->name == rune::Name("Keg")) {
            keg = a;
        }
    }
    REQUIRE(keg != nullptr);
    CHECK(rune::actor::Role(keg) == rune::net::ROLE_DumbProxy);
    CHECK(rune::actor::RemoteRole(keg) == rune::net::ROLE_Authority);
    rune::Vector kegLocation = rune::actor::Location(keg);
    bool sounding = false;
    waitFor([&]() { client.tick(); }, [&]() {
        for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
            sounding = sounding || voice.actor == keg;
        }
        return sounding; });
    CHECK(sounding);
    CHECK(client.world->destroy(keg) == false);
    CHECK_FALSE(keg->destroyed);

    rune::UObject* console = rune::player::Console(client.world->viewport);
    REQUIRE(console != nullptr);
    rune::console::execute(client.world->player(), "say hello over the wire");
    client.tick(30);
    bool heard = false;
    for (int i = 0; i < 64; i++) {
        std::string line = *reinterpret_cast<std::string*>(console->propertyAddress(rune::Name("MsgText"), i));
        heard = heard || line.find("hello over the wire") != std::string::npos;
    }
    CHECK(heard);
    ServerReport report = server.stop();
    CHECK(kegLocation.x == Catch::Approx(report.kegX).margin(0.01));
    CHECK(kegLocation.y == Catch::Approx(report.kegY).margin(0.01));
    CHECK(kegLocation.z == Catch::Approx(report.kegZ).margin(0.01));
    rune::audio::shutdown();
}
#endif

#ifndef _WIN32
namespace {
    std::string httpGet(uint16_t port, const std::string& request)
    {
        rune::net::Socket socket;
        REQUIRE(socket.open(rune::net::Socket::Kind::Tcp));
        REQUIRE(socket.connect({ 0x7f000001, port }));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!socket.connected() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(socket.send(request.data(), request.size()) == static_cast<int64_t>(request.size()));
        std::string response;
        char buffer[4096];
        while (!socket.failed() && std::chrono::steady_clock::now() < deadline) {
            int64_t received = socket.receive(buffer, sizeof(buffer));
            if (received > 0) {
                response.append(buffer, static_cast<size_t>(received));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        return response;
    }
}

TEST_CASE("the web admin answers over TcpLink, the server query answers over UdpLink and an unresolvable master server does not stop the server", "[data][net]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    ServerProcess server({ .services = true });
    REQUIRE(server.started.webPort != 0);
    REQUIRE(server.started.queryPort != 0);

    std::string denied = httpGet(server.started.webPort, "GET /ServerAdmin/current HTTP/1.0\r\n\r\n");
    CHECK(denied.rfind("HTTP/1.1 401 Unauthorized\r\n", 0) == 0);
    CHECK(denied.find("basic realm=\"Rune Remote Admin Server\"") != std::string::npos);

    std::string page = httpGet(server.started.webPort, "GET /ServerAdmin/current_game HTTP/1.0\r\nAuthorization: Basic YWRtaW46YWRtaW4=\r\n\r\n");
    CHECK(page.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(page.find("<option value=\"RuneI.RuneMultiPlayer\" selected>RuneMatch</option>") != std::string::npos);
    CHECK(page.find("%GameTypeSelect%") == std::string::npos);

    rune::net::Socket query;
    REQUIRE(query.open(rune::net::Socket::Kind::Udp));
    std::string info = "\\info\\";
    REQUIRE(query.sendTo(info.data(), info.size(), { 0x7f000001, server.started.queryPort }) == static_cast<int64_t>(info.size()));
    std::string answer;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (answer.empty() && std::chrono::steady_clock::now() < deadline) {
        char buffer[2048];
        rune::net::Address from;
        int64_t received = query.receiveFrom(buffer, sizeof(buffer), from);
        if (received > 0) {
            answer.assign(buffer, static_cast<size_t>(received));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK(answer.find("\\mapname\\DM-Hel\\") != std::string::npos);
    CHECK(answer.find("\\gametype\\RuneMultiPlayer\\") != std::string::npos);
    CHECK(answer.find("\\hostport\\" + std::to_string(server.started.port) + "\\") != std::string::npos);
    CHECK(answer.find("\\numplayers\\0\\") != std::string::npos);
    server.stop();
}
#endif

#ifndef _WIN32
TEST_CASE("two clients see each other as simulated proxies and a leaving player disappears for the other", "[data][net]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    bool clean = GENERATE(true, false);
    ServerProcess server;
    PeerProcess peer(server.started.port);
    Client client(server.started.port);
    rune::UObject* other = nullptr;
    waitFor([&]() { client.tick(); }, [&]() {
        other = client.remotePlayer("Second");
        return other != nullptr; });
    REQUIRE(other != nullptr);
    CHECK(rune::actor::Role(other) == rune::net::ROLE_SimulatedProxy);
    CHECK(rune::actor::RemoteRole(other) == rune::net::ROLE_Authority);
    rune::Vector start = rune::actor::Location(other);
    rune::Vector walked = peer.walk();
    client.tick(90);
    rune::Vector seen = rune::actor::Location(other);
    CHECK((seen - start).size() > 50.0f);
    CHECK((seen - walked).size() < 30.0f);

    if (clean) {
        peer.leave();
    } else {
        peer.drop();
    }
    waitFor([&]() { client.tick(); }, [&]() { return other->destroyed && !client.hasPlayerInfo("Second"); });
    CHECK(other->destroyed);
    CHECK_FALSE(client.hasPlayerInfo("Second"));
    client.tick(30);
    ServerReport report = server.stop();
    CHECK(report.players == 1);
    CHECK(findPlayer(report, "NetTester") != nullptr);
    CHECK(findPlayer(report, "Second") == nullptr);
    CHECK(report.connections == 1);
}
#endif

#ifndef _WIN32
TEST_CASE("a server travel takes connected clients to the new map on the same connection", "[data][net]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    ServerProcess server;
    Client client(server.started.port);
    client.tick(30);
    REQUIRE(client.map() == "DM-Hel");
    rune::net::Driver* driver = client.driver.get();
    server.travel();
    waitFor([&]() { client.tick(); }, [&]() { return client.map() == "DM-Ruins"; });
    CHECK(client.map() == "DM-Ruins");
    CHECK(client.driver.get() == driver);
    CHECK_FALSE(driver->disconnected());
    CHECK(client.ignoredTravel == "DM-Ruins");
    CHECK(client.ignoredTravelType == rune::TRAVEL_Relative);
    rune::UObject* pawn = client.world->player();
    REQUIRE(pawn != nullptr);
    CHECK(rune::actor::Role(pawn) == rune::net::ROLE_AutonomousProxy);
    client.tick(30);
    rune::UObject* info = rune::pawn::PlayerReplicationInfo(pawn);
    REQUIRE(info != nullptr);
    CHECK(rune::playerreplicationinfo::PlayerName(info) == "NetTester");
    ServerReport report = server.stop();
    CHECK(std::string(report.map) == "DM-Ruins");
    CHECK(report.players == 1);
    CHECK(findPlayer(report, "NetTester") != nullptr);
    CHECK(report.connections == 1);
}
#endif

#ifndef _WIN32
TEST_CASE("a listen server's local player and a joining client see each other", "[data][net]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    ServerProcess server({ .listen = true });
    Client client(server.started.port);
    rune::UObject* host = nullptr;
    waitFor([&]() { client.tick(); }, [&]() {
        host = client.remotePlayer("Host");
        return host != nullptr; });
    REQUIRE(host != nullptr);
    CHECK(rune::actor::Role(host) == rune::net::ROLE_SimulatedProxy);
    client.tick(30);
    rune::Vector own = rune::actor::Location(client.world->player());
    ServerReport report = server.stop();
    CHECK(report.players == 2);
    const PlayerReport* local = findPlayer(report, "Host");
    REQUIRE(local != nullptr);
    CHECK((rune::Vector { local->x, local->y, local->z } - rune::actor::Location(host)).size() < 30.0f);
    const PlayerReport* tester = findPlayer(report, "NetTester");
    REQUIRE(tester != nullptr);
    CHECK((rune::Vector { tester->x, tester->y, tester->z } - own).size() < 30.0f);
    CHECK(report.connections == 1);
}
#endif
