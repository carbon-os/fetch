#pragma once
#include "request.hpp"
#include "response.hpp"
#include <functional>

namespace fetch {

struct ClientOptions {
    std::string base_url;
    Headers     headers;
    std::chrono::milliseconds timeout{30'000};
    bool        verify_ssl       = true;
    bool        follow_redirects = true;
    int         max_redirects    = 10;
};

// Represents a single Server-Sent Event
struct ServerEvent {
    std::string id;
    std::string event = "message"; // "message" is the spec default
    std::string data;
    int retry = 0;
};

using SseCallback = std::function<bool(const ServerEvent&)>;

class Client {
public:
    explicit Client(ClientOptions opts = {});

    Response get  (const std::string& path, const Options& opts = {}) const;
    Response post (const std::string& path, const std::string& body, const Options& opts = {}) const;
    Response post (const std::string& path, const Form& form,        const Options& opts = {}) const;
    Response put  (const std::string& path, const std::string& body, const Options& opts = {}) const;
    Response patch(const std::string& path, const std::string& body, const Options& opts = {}) const;
    Response del  (const std::string& path, const Options& opts = {}) const;

    Response request(const std::string& method, const std::string& path,
                     const Options& opts = {}) const;

    // --- Server-Sent Events (SSE) ---
    // The callback should return `true` to keep listening, or `false` to hang up.
    void sse(const std::string& path, SseCallback cb, const Options& opts = {}) const;

private:
    ClientOptions opts_;
};

// --- Free functions ---
Response get  (const std::string& url, const Options& opts = {});
Response post (const std::string& url, const std::string& body, const Options& opts = {});
Response post (const std::string& url, const Form& form,        const Options& opts = {});
Response put  (const std::string& url, const std::string& body, const Options& opts = {});
Response patch(const std::string& url, const std::string& body, const Options& opts = {});
Response del  (const std::string& url, const Options& opts = {});

// --- SSE Free function ---
void sse(const std::string& url, SseCallback cb, const Options& opts = {});

} // namespace fetch