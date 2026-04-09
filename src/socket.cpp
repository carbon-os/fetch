#include <fetch/detail/socket.hpp>
#include <fetch/error.hpp>
#include <cstring>

#ifdef _WIN32
  namespace { struct WsaInit {
      WsaInit()  { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
     ~WsaInit()  { WSACleanup(); }
  } g_wsa; }
  #define SOCK_CLOSE  closesocket
  #define GET_ERR()   WSAGetLastError()
  #define WOULD_BLOCK WSAEWOULDBLOCK
#else
  #include <sys/socket.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #define SOCK_CLOSE  ::close
  #define GET_ERR()   errno
  #define WOULD_BLOCK EINPROGRESS
#endif

namespace fetch::detail {

TcpSocket::TcpSocket()  = default;
TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = invalid_socket; }
TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept {
    if (this != &o) { close(); fd_ = o.fd_; o.fd_ = invalid_socket; }
    return *this;
}

void TcpSocket::connect(const std::string& host, uint16_t port,
                        std::chrono::milliseconds timeout) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
        throw fetch::NetworkError("DNS resolution failed for: " + host);

    fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd_ == invalid_socket) { freeaddrinfo(res); throw fetch::NetworkError("socket() failed"); }

    // Non-blocking for timeout connect
#ifdef _WIN32
    u_long nb = 1; ioctlsocket(fd_, FIONBIO, &nb);
#else
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif

    int r = ::connect(fd_, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen));
    freeaddrinfo(res);

    if (r != 0 && GET_ERR() != WOULD_BLOCK) {
        close();
        throw fetch::NetworkError("connect() failed to " + host);
    }

    fd_set wfds; FD_ZERO(&wfds); FD_SET(fd_, &wfds);
    auto ms = timeout.count();
    timeval tv{
        static_cast<long>(ms / 1000),
        static_cast<suseconds_t>((ms % 1000) * 1000)
    };

    r = select(static_cast<int>(fd_ + 1), nullptr, &wfds, nullptr, &tv);
    if (r == 0) { close(); throw fetch::TimeoutError{}; }
    if (r <  0) { close(); throw fetch::NetworkError("select() failed"); }

    int       err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
    if (err != 0) { close(); throw fetch::NetworkError("connect failed: " + std::to_string(err)); }

    // Restore blocking
#ifdef _WIN32
    nb = 0; ioctlsocket(fd_, FIONBIO, &nb);
#else
    fcntl(fd_, F_SETFL, flags);
#endif

    // Apply timeout to all subsequent recv calls
#ifdef _WIN32
    DWORD rcv_ms = static_cast<DWORD>(timeout.count());
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&rcv_ms), sizeof(rcv_ms));
#else
    timeval rcv_tv{
        static_cast<long>(ms / 1000),
        static_cast<suseconds_t>((ms % 1000) * 1000)
    };
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&rcv_tv), sizeof(rcv_tv));
#endif
}

void TcpSocket::close() {
    if (fd_ != invalid_socket) { SOCK_CLOSE(fd_); fd_ = invalid_socket; }
}

ssize_t TcpSocket::send(const char* data, size_t len) {
#ifdef _WIN32
    return ::send(fd_, data, static_cast<int>(len), 0);
#else
    return ::send(fd_, data, len, MSG_NOSIGNAL);
#endif
}

ssize_t TcpSocket::recv(char* buf, size_t len) {
#ifdef _WIN32
    ssize_t n = ::recv(fd_, buf, static_cast<int>(len), 0);
    if (n < 0 && WSAGetLastError() == WSAETIMEDOUT) throw fetch::TimeoutError{};
    return n;
#else
    ssize_t n = ::recv(fd_, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) throw fetch::TimeoutError{};
    return n;
#endif
}

} // namespace fetch::detail