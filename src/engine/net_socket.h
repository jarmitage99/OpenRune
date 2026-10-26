#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rune::net {
    struct Address {
        uint32_t ip = 0;
        uint16_t port = 0;
    };

    bool resolve(const std::string& host, uint32_t& ip);
    std::string ipText(uint32_t ip);
    uint32_t localIp();

    class Socket {
    public:
        enum class Kind : uint8_t {
            Tcp,
            Udp,
        };

        Socket() = default;
        ~Socket();
        Socket(Socket&& other) noexcept;
        Socket& operator=(Socket&& other) noexcept;
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        bool open(Kind kind);
        bool bind(uint16_t port);
        bool listen();
        bool accept(Socket& client, Address& from);
        bool connect(const Address& to);
        bool connected();
        bool broadcast();
        int64_t send(const void* data, size_t size);
        int64_t receive(void* data, size_t size);
        int64_t sendTo(const void* data, size_t size, const Address& to);
        int64_t receiveFrom(void* data, size_t size, Address& from);
        uint16_t localPort() const;
        bool valid() const;
        bool failed() const;
        void close();

    private:
        intptr_t handle = -1;
        bool broken = false;
        bool pending = false;
    };
}
