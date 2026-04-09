#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
#include <chrono>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  using ssize_t  = int;
  static constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
  using socket_t = int;
  static constexpr socket_t invalid_socket = -1;
#endif

namespace fetch::detail {

class TcpSocket {
public:
    TcpSocket();
   ~TcpSocket();
    TcpSocket(const TcpSocket&)            = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&&) noexcept;
    TcpSocket& operator=(TcpSocket&&) noexcept;

    void    connect(const std::string& host, uint16_t port,
                    std::chrono::milliseconds timeout);
    void    close();
    ssize_t send(const char* data, size_t len);
    ssize_t recv(char* buf,        size_t len);
    socket_t fd() const { return fd_; }

private:
    socket_t fd_ = invalid_socket;
};

} // namespace fetch::detail