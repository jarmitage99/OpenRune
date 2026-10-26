#pragma once

#include "core/object.h"
#include "engine/net_socket.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rune {
    class World;

    namespace net {
        constexpr uint8_t NM_Standalone = 0;
        constexpr uint8_t NM_DedicatedServer = 1;
        constexpr uint8_t NM_ListenServer = 2;
        constexpr uint8_t NM_Client = 3;

        constexpr uint8_t ROLE_None = 0;
        constexpr uint8_t ROLE_DumbProxy = 1;
        constexpr uint8_t ROLE_SimulatedProxy = 2;
        constexpr uint8_t ROLE_AutonomousProxy = 3;
        constexpr uint8_t ROLE_Authority = 4;

        constexpr uint16_t DefaultPort = 7777;
        constexpr uint32_t ProtocolVersion = 1;

        struct Channel {
            uint32_t id = 0;
            UObject* actor = nullptr;
            std::unordered_map<uint32_t, std::vector<uint8_t>> sent;
            bool initial = true;
            double nextUpdate = 0;
            double lastRelevant = 0;
        };

        struct Connection {
            Socket socket;
            Address address;
            std::vector<uint8_t> input;
            std::vector<uint8_t> output;
            UObject* player = nullptr;
            UObject* actor = nullptr;
            std::string options;
            bool welcomed = false;
            bool joined = false;
            bool closing = false;
            uint32_t nextId = 1;
            std::unordered_map<UObject*, Channel> channels;
            std::unordered_map<uint32_t, UObject*> byId;
            std::unordered_set<UObject*> tornOff;
        };

        class Driver {
        public:
            ~Driver();

            static std::unique_ptr<Driver> listen(uint16_t port, std::string& error);
            static std::unique_ptr<Driver> connect(const std::string& host, uint16_t port, const std::string& options, std::string& error);

            bool server() const;
            uint16_t port() const;
            void attach(World* target);
            void tick(float deltaSeconds);
            bool waitForWelcome(double seconds, std::string& map, std::string& error);
            void join();
            bool disconnected() const;
            bool takeWelcome(std::string& map);
            std::string reason() const;
            void actorDestroyed(UObject* actor);
            bool remoteCall(UObject* object, UFunction* function, uint8_t* parameters);
            size_t connectionCount() const;
            Connection* connectionOf(UObject* actor);

        private:
            Driver() = default;

            void acceptConnections();
            void receive(Connection& connection);
            void process(Connection& connection, uint8_t type, const uint8_t* data, size_t size);
            void flush(Connection& connection);
            void replicateServer();
            void replicateClient();
            void replicate(Connection& connection, Channel& channel, bool toClient);
            void openChannel(Connection& connection, UObject* actor);
            void closeChannel(Connection& connection, UObject* actor, bool keepActor);
            bool relevant(Connection& connection, UObject* actor, int depth = 0);
            void login(Connection& connection);
            void receiveOpen(Connection& connection, const uint8_t* data, size_t size);
            void receiveUpdate(Connection& connection, const uint8_t* data, size_t size);
            void receiveClose(Connection& connection, const uint8_t* data, size_t size);
            void receiveCall(Connection& connection, const uint8_t* data, size_t size);
            void receivePossess(Connection& connection, const uint8_t* data, size_t size);

            bool isServer = false;
            World* world = nullptr;
            Socket listener;
            uint16_t listenPort = 0;
            std::vector<std::unique_ptr<Connection>> connections;
            std::string welcomeMap;
            double time = 0;
            double nextReplication = 0;
        };

        Driver* active();
        bool isHostUrl(const std::string& map);
    }
}
