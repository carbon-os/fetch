#include <fetch/detail/tls.hpp>
#include <fetch/error.hpp>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <cerrno>

namespace fetch::detail {

TlsSocket::TlsSocket(bool verify_ssl) : verify_ssl_(verify_ssl) {
    ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ctx_) throw fetch::SslError("SSL_CTX_new failed");

    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);

    if (verify_ssl_) {
        SSL_CTX_set_default_verify_paths(ctx_);
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
    } else {
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
    }
}

TlsSocket::~TlsSocket() {
    close();
    if (ssl_) { SSL_free(ssl_); ssl_ = nullptr; }
    if (ctx_) { SSL_CTX_free(ctx_); ctx_ = nullptr; }
}

void TlsSocket::connect(const std::string& host, uint16_t port,
                        std::chrono::milliseconds timeout) {
    tcp_.connect(host, port, timeout);

    ssl_ = SSL_new(ctx_);
    if (!ssl_) throw fetch::SslError("SSL_new failed");

    SSL_set_tlsext_host_name(ssl_, host.c_str());

    if (verify_ssl_) {
        SSL_set_hostflags(ssl_, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        SSL_set1_host(ssl_, host.c_str());
    }

    SSL_set_fd(ssl_, static_cast<int>(tcp_.fd()));

    if (SSL_connect(ssl_) != 1)
        throw fetch::SslError("TLS handshake failed: " + ssl_error_string());
}

void TlsSocket::close() {
    if (ssl_) SSL_shutdown(ssl_);
    tcp_.close();
}

ssize_t TlsSocket::send(const char* data, size_t len) {
    return SSL_write(ssl_, data, static_cast<int>(len));
}

ssize_t TlsSocket::recv(char* buf, size_t len) {
    int n = SSL_read(ssl_, buf, static_cast<int>(len));
    if (n <= 0) {
        int ssl_err = SSL_get_error(ssl_, n);

        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            throw fetch::TimeoutError{};
        }

        if (ssl_err == SSL_ERROR_SYSCALL &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)) {
            throw fetch::TimeoutError{};
        }
    }
    return static_cast<ssize_t>(n);
}

std::string TlsSocket::ssl_error_string() {
    unsigned long e;
    std::string out;
    while ((e = ERR_get_error()) != 0) {
        char buf[256]; ERR_error_string_n(e, buf, sizeof(buf));
        if (!out.empty()) out += "; ";
        out += buf;
    }
    return out.empty() ? "unknown SSL error" : out;
}

} // namespace fetch::detail