#include "engine/net_socket.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rune::net {
    namespace {
#ifdef _WIN32
        using Native = SOCKET;
        using Length = int;

        bool startup()
        {
            static bool started = [] {
                WSADATA data;
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            return started;
        }

        bool wouldBlock()
        {
            int error = WSAGetLastError();
            return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
        }

        void closeNative(Native socket)
        {
            closesocket(socket);
        }

        bool setNonBlocking(Native socket)
        {
            u_long enabled = 1;
            return ioctlsocket(socket, static_cast<long>(FIONBIO), &enabled) == 0;
        }
#else
        using Native = int;
        using Length = socklen_t;

        bool startup()
        {
            return true;
        }

        bool wouldBlock()
        {
            return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY;
        }

        void closeNative(Native socket)
        {
            ::close(socket);
        }

        bool setNonBlocking(Native socket)
        {
            int flags = fcntl(socket, F_GETFL, 0);
            return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
        }
#endif

        Native native(intptr_t handle)
        {
            return static_cast<Native>(handle);
        }

        sockaddr_in socketAddress(const Address& address)
        {
            sockaddr_in result {};
            result.sin_family = AF_INET;
            result.sin_addr.s_addr = htonl(address.ip);
            result.sin_port = htons(address.port);
            return result;
        }

        Address fromSocketAddress(const sockaddr_in& address)
        {
            return { ntohl(address.sin_addr.s_addr), ntohs(address.sin_port) };
        }
    }

    bool resolve(const std::string& host, uint32_t& ip)
    {
        if (!startup()) {
            return false;
        }
        in_addr parsed {};
        if (inet_pton(AF_INET, host.c_str(), &parsed) == 1) {
            ip = ntohl(parsed.s_addr);
            return true;
        }
        addrinfo hints {};
        hints.ai_family = AF_INET;
        addrinfo* found = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || !found) {
            return false;
        }
        ip = ntohl(reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr);
        freeaddrinfo(found);
        return true;
    }

    std::string ipText(uint32_t ip)
    {
        return std::to_string((ip >> 24) & 255) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
    }

    uint32_t localIp()
    {
        char host[256] = {};
        uint32_t ip = 0x7f000001;
        if (startup() && gethostname(host, sizeof(host) - 1) == 0) {
            resolve(host, ip);
        }
        return ip;
    }

    Socket::~Socket()
    {
        close();
    }

    Socket::Socket(Socket&& other) noexcept
        : handle(other.handle)
        , broken(other.broken)
        , pending(other.pending)
    {
        other.handle = -1;
    }

    Socket& Socket::operator=(Socket&& other) noexcept
    {
        if (this != &other) {
            close();
            handle = other.handle;
            broken = other.broken;
            pending = other.pending;
            other.handle = -1;
        }
        return *this;
    }

    bool Socket::open(Kind kind)
    {
        close();
        if (!startup()) {
            return false;
        }
        Native created = kind == Kind::Tcp ? ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
        if (created == INVALID_SOCKET) {
            return false;
        }
#else
        if (created < 0) {
            return false;
        }
#endif
        handle = static_cast<intptr_t>(created);
        broken = false;
        pending = false;
        int enabled = 1;
        if (kind == Kind::Tcp) {
            setsockopt(created, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
            setsockopt(created, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
        }
        return setNonBlocking(created);
    }

    bool Socket::bind(uint16_t port)
    {
        sockaddr_in address = socketAddress({ 0, port });
        return ::bind(native(handle), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }

    bool Socket::listen()
    {
        return ::listen(native(handle), 16) == 0;
    }

    bool Socket::accept(Socket& client, Address& from)
    {
        sockaddr_in address {};
        Length length = sizeof(address);
        Native accepted = ::accept(native(handle), reinterpret_cast<sockaddr*>(&address), &length);
#ifdef _WIN32
        if (accepted == INVALID_SOCKET) {
            return false;
        }
#else
        if (accepted < 0) {
            return false;
        }
#endif
        client.close();
        client.handle = static_cast<intptr_t>(accepted);
        client.broken = false;
        client.pending = false;
        int enabled = 1;
        setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
        setNonBlocking(accepted);
        from = fromSocketAddress(address);
        return true;
    }

    bool Socket::connect(const Address& to)
    {
        sockaddr_in address = socketAddress(to);
        if (::connect(native(handle), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            return true;
        }
        if (wouldBlock()) {
            pending = true;
            return true;
        }
        broken = true;
        return false;
    }

    bool Socket::connected()
    {
        if (!valid() || broken) {
            return false;
        }
        if (!pending) {
            return true;
        }
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(native(handle), &writable);
        timeval zero {};
        if (select(static_cast<int>(native(handle) + 1), nullptr, &writable, nullptr, &zero) <= 0) {
            return false;
        }
        int error = 0;
        Length length = sizeof(error);
        getsockopt(native(handle), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
        if (error != 0) {
            broken = true;
            return false;
        }
        pending = false;
        return true;
    }

    bool Socket::broadcast()
    {
        int enabled = 1;
        return setsockopt(native(handle), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
    }

    int64_t Socket::send(const void* data, size_t size)
    {
        if (!connected()) {
            return 0;
        }
#ifdef _WIN32
        int sent = ::send(native(handle), static_cast<const char*>(data), static_cast<int>(size), 0);
#elif defined(MSG_NOSIGNAL)
        ssize_t sent = ::send(native(handle), data, size, MSG_NOSIGNAL);
#else
        ssize_t sent = ::send(native(handle), data, size, 0);
#endif
        if (sent < 0) {
            if (!wouldBlock()) {
                broken = true;
            }
            return 0;
        }
        return static_cast<int64_t>(sent);
    }

    int64_t Socket::receive(void* data, size_t size)
    {
        if (!connected()) {
            return 0;
        }
#ifdef _WIN32
        int received = ::recv(native(handle), static_cast<char*>(data), static_cast<int>(size), 0);
#else
        ssize_t received = ::recv(native(handle), data, size, 0);
#endif
        if (received == 0) {
            broken = true;
            return 0;
        }
        if (received < 0) {
            if (!wouldBlock()) {
                broken = true;
            }
            return 0;
        }
        return static_cast<int64_t>(received);
    }

    int64_t Socket::sendTo(const void* data, size_t size, const Address& to)
    {
        sockaddr_in address = socketAddress(to);
#ifdef _WIN32
        int sent = ::sendto(native(handle), static_cast<const char*>(data), static_cast<int>(size), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
#else
        ssize_t sent = ::sendto(native(handle), data, size, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
#endif
        return sent < 0 ? 0 : static_cast<int64_t>(sent);
    }

    int64_t Socket::receiveFrom(void* data, size_t size, Address& from)
    {
        sockaddr_in address {};
        Length length = sizeof(address);
#ifdef _WIN32
        int received = ::recvfrom(native(handle), static_cast<char*>(data), static_cast<int>(size), 0, reinterpret_cast<sockaddr*>(&address), &length);
#else
        ssize_t received = ::recvfrom(native(handle), data, size, 0, reinterpret_cast<sockaddr*>(&address), &length);
#endif
        if (received < 0) {
            return -1;
        }
        from = fromSocketAddress(address);
        return static_cast<int64_t>(received);
    }

    uint16_t Socket::localPort() const
    {
        sockaddr_in address {};
        Length length = sizeof(address);
        if (getsockname(native(handle), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return 0;
        }
        return ntohs(address.sin_port);
    }

    bool Socket::valid() const
    {
        return handle != -1;
    }

    bool Socket::failed() const
    {
        return broken;
    }

    void Socket::close()
    {
        if (valid()) {
            closeNative(native(handle));
            handle = -1;
        }
        broken = false;
        pending = false;
    }
}
