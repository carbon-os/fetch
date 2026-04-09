#pragma once
#include "socket.hpp"
#include <openssl/ssl.h>
#include <string>

namespace fetch::detail {

class TlsSocket {
public:
    explicit TlsSocket(bool verify_ssl = true);
   ~TlsSocket();
    TlsSocket(const TlsSocket&)            = delete;
    TlsSocket& operator=(const TlsSocket&) = delete;

    void    connect(const std::string& host, uint16_t port,
                    std::chrono::milliseconds timeout);
    void    close();
    ssize_t send(const char* data, size_t len);
    ssize_t recv(char* buf,        size_t len);

private:
    TcpSocket tcp_;
    SSL_CTX*  ctx_        = nullptr;
    SSL*      ssl_        = nullptr;
    bool      verify_ssl_;

    std::string ssl_error_string();
};

} // namespace fetch::detail