#pragma once
#include <stdexcept>
#include <string>

namespace fetch {

class FetchError : public std::runtime_error {
public:
    explicit FetchError(const std::string& msg) : std::runtime_error(msg) {}
};

class NetworkError : public FetchError {
public:
    explicit NetworkError(const std::string& msg)
        : FetchError("Network error: " + msg) {}
};

class SslError : public FetchError {
public:
    explicit SslError(const std::string& msg)
        : FetchError("SSL error: " + msg) {}
};

class HttpError : public FetchError {
public:
    HttpError(int status, const std::string& msg)
        : FetchError("HTTP " + std::to_string(status) + ": " + msg)
        , status_(status) {}
    int status() const { return status_; }
private:
    int status_;
};

class TimeoutError : public NetworkError {
public:
    TimeoutError() : NetworkError("request timed out") {}
};

} // namespace fetch