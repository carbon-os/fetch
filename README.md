# fetch

A modern C++20 HTTP client library with a clean, expressive API. Supports HTTPS via OpenSSL, chunked transfer encoding, redirect following, form encoding, Server-Sent Events, and a reusable `Client` for base-URL workflows — plus a `curl`-like CLI tool.

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
  - [Free Functions](#free-functions)
  - [Options](#options)
  - [Response](#response)
  - [Client](#client)
  - [Server-Sent Events](#server-sent-events)
  - [Error Handling](#error-handling)
- [CLI Tool](#cli-tool)
- [Project Layout](#project-layout)
- [License](#license)

---

## Features

- **GET, POST, PUT, PATCH, DELETE** — free functions and a reusable `Client`
- **HTTPS** with full certificate verification (OpenSSL 3)
- **Chunked transfer encoding** decoded transparently
- **Redirect following** with configurable hop limit and 303 method rewriting
- **Form encoding** via `fetch::Form`
- **Server-Sent Events** with a streaming callback API
- **Per-request timeouts** using `std::chrono::milliseconds`
- **Case-insensitive headers** via a custom `std::map` comparator
- **Raw bytes** access via `Response::bytes()`
- **Cross-platform** — Linux, macOS, Windows (Winsock)
- **Zero external runtime dependencies** beyond OpenSSL

---

## Requirements

| Dependency | Version  | Notes                          |
|------------|----------|--------------------------------|
| C++        | 20+      | Tested with GCC 13, Clang 16, MSVC 19.38 |
| CMake      | 3.25+    |                                |
| OpenSSL    | 3.0+     | Managed via vcpkg (see below)  |

### Installing OpenSSL

This library is distributed as a vcpkg package, so OpenSSL is acquired through vcpkg automatically:

```bash
vcpkg install fetch
```

---

## Building

```bash
git clone https://github.com/carbon-os/fetch.git
cd fetch
cmake -B build
cmake --build build
```

### CMake options

| Option                | Default | Description                    |
|-----------------------|---------|--------------------------------|
| `FETCH_BUILD_TESTS`   | `ON`    | Build the test suite           |
| `FETCH_BUILD_CLI`     | `ON`    | Build the `fetch` CLI tool     |
| `CMAKE_BUILD_TYPE`    | `Release` | `Debug` / `Release` / `RelWithDebInfo` |

```bash
# Library only, no tests, no CLI
cmake -B build -DFETCH_BUILD_TESTS=OFF -DFETCH_BUILD_CLI=OFF
cmake --build build

# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Running tests

```bash
ctest --test-dir build --output-on-failure
```

### Installing

```bash
cmake --install build --prefix /usr/local
```

This installs:
- `libfetch.a` / `libfetch.so` → `lib/`
- Headers → `include/fetch/`
- `fetch` CLI binary → `bin/`
- CMake package files → `lib/cmake/fetch/`

---

## Quick Start

```cpp
#include <fetch/fetch.hpp>
#include <iostream>

int main() {
    // Simple GET
    auto res = fetch::get("https://httpbin.org/get");
    std::cout << res.status() << "\n";   // 200
    std::cout << res.text()   << "\n";   // JSON body

    // POST with JSON
    auto post = fetch::post(
        "https://httpbin.org/post",
        R"({"name":"alice"})",
        { .headers = {{"Content-Type", "application/json"}} }
    );
    std::cout << post.ok() << "\n";      // 1

    // POST a form
    fetch::Form form{{"username", "alice"}, {"password", "secret"}};
    auto form_res = fetch::post("https://httpbin.org/post", form);

    // Reusable client with a base URL and shared headers
    fetch::Client client({
        .base_url = "https://jsonplaceholder.typicode.com",
        .headers  = {{"Accept", "application/json"}},
    });
    auto posts = client.get("/posts");
    auto one   = client.get("/posts/1");
}
```

---

## API Reference

### Free Functions

```cpp
namespace fetch {

Response get  (const std::string& url, const Options& opts = {});
Response post (const std::string& url, const std::string& body, const Options& opts = {});
Response post (const std::string& url, const Form& form,        const Options& opts = {});
Response put  (const std::string& url, const std::string& body, const Options& opts = {});
Response patch(const std::string& url, const std::string& body, const Options& opts = {});
Response del  (const std::string& url, const Options& opts = {});

void sse(const std::string& url, SseCallback cb, const Options& opts = {});

} // namespace fetch
```

---

### Options

All request-level settings are carried in `fetch::Options`:

```cpp
struct Options {
    std::string               method           = "GET";
    Headers                   headers;                      // case-insensitive map
    std::string               body;
    std::chrono::milliseconds timeout{30'000};              // 30 s default
    bool                      follow_redirects = true;
    int                       max_redirects    = 10;
    bool                      verify_ssl       = true;
};
```

Options are passed inline using C++20 designated initializers:

```cpp
fetch::get("https://example.com", {
    .headers = {
        {"Authorization", "Bearer token123"},
        {"Accept",        "application/json"},
    },
    .timeout        = std::chrono::milliseconds(5000),
    .verify_ssl     = false,
    .max_redirects  = 3,
});
```

#### Form fields

```cpp
// fetch::Form wraps a std::map<string, string>
// Values are automatically percent-encoded.
fetch::Form form{
    {"query",    "hello world"},
    {"lang",     "en"},
};
auto res = fetch::post("https://httpbin.org/post", form);
```

---

### Response

```cpp
class Response {
public:
    int                status()      const;  // e.g. 200
    const std::string& status_text() const;  // e.g. "OK"
    bool               ok()          const;  // true if 200–299

    const Headers&     headers()     const;
    std::string        header(std::string_view name) const; // case-insensitive lookup

    const std::string& text()        const;  // body as UTF-8 string
    std::vector<std::byte> bytes()   const;  // body as raw bytes
};
```

```cpp
auto res = fetch::get("https://httpbin.org/get");

if (res.ok()) {
    std::cout << res.text() << "\n";
}

// Header lookup is case-insensitive
std::cout << res.header("content-type") << "\n";
std::cout << res.header("Content-Type") << "\n";  // same result

// Raw bytes — useful for binary payloads
auto bytes = res.bytes();
std::ofstream f("image.png", std::ios::binary);
for (auto b : bytes) f.put(static_cast<char>(b));
```

---

### Client

`fetch::Client` holds a base URL and a set of default headers that are merged into every request it makes. Use it when you are talking to a single API across multiple calls.

```cpp
fetch::Client client({
    .base_url         = "https://api.example.com",
    .headers          = {{"Authorization", "Bearer <token>"}},
    .timeout          = std::chrono::milliseconds(10'000),
    .verify_ssl       = true,
    .follow_redirects = true,
    .max_redirects    = 5,
});

// Paths are appended to base_url
auto res  = client.get("/users");
auto user = client.get("/users/42");

// Per-request headers are merged on top of the client defaults
auto created = client.post(
    "/users",
    R"({"name":"bob"})",
    { .headers = {{"Content-Type", "application/json"}} }
);
std::cout << created.status() << "\n";  // 201

// All methods are available
client.put   ("/users/42", R"({"name":"bobby"})");
client.patch ("/users/42", R"({"name":"rob"})");
client.del   ("/users/42");

// Arbitrary method
client.request("OPTIONS", "/users");
```

---

### Server-Sent Events

`fetch::sse()` opens a persistent HTTP connection and invokes a callback for each event emitted by the server. Return `true` from the callback to keep listening, `false` to close the connection.

```cpp
// fetch::ServerEvent fields:
//   std::string id;
//   std::string event;   // defaults to "message"
//   std::string data;
//   int         retry;   // server reconnect hint, ms (0 if not set)

fetch::sse("https://example.com/stream", [](const fetch::ServerEvent& ev) -> bool {
    std::cout << "event : " << ev.event << "\n";
    std::cout << "data  : " << ev.data  << "\n";
    if (!ev.id.empty())
        std::cout << "id    : " << ev.id << "\n";

    // Stop after seeing a "done" event
    return ev.event != "done";
});
```

Both plain HTTP and HTTPS endpoints are supported. Chunked transfer encoding is handled transparently.

---

### Error Handling

All exceptions derive from `fetch::FetchError` → `std::runtime_error`.

```cpp
namespace fetch {
    class FetchError   : public std::runtime_error { … };
    class NetworkError : public FetchError { … };   // DNS, connect, send/recv
    class SslError     : public FetchError { … };   // TLS handshake / cert
    class HttpError    : public FetchError {        // non-2xx (thrown by SSE only)
        int status() const;
    };
    class TimeoutError : public NetworkError { … }; // connect or recv timeout
}
```

```cpp
try {
    auto res = fetch::get("https://example.com/api", {
        .timeout    = std::chrono::milliseconds(3000),
        .verify_ssl = true,
    });

    if (!res.ok())
        std::cerr << "HTTP error: " << res.status() << "\n";

} catch (const fetch::TimeoutError&  e) { std::cerr << "Timed out: "   << e.what() << "\n"; }
  catch (const fetch::SslError&      e) { std::cerr << "TLS error: "   << e.what() << "\n"; }
  catch (const fetch::NetworkError&  e) { std::cerr << "Network: "     << e.what() << "\n"; }
  catch (const fetch::FetchError&    e) { std::cerr << "fetch error: " << e.what() << "\n"; }
```

> **Note:** `NetworkError`, `SslError`, and `TimeoutError` are all thrown for connection-level failures. Regular HTTP error status codes (4xx, 5xx) are **not** thrown — check `res.ok()` or `res.status()` instead.

---

## CLI Tool

The `fetch` binary mirrors the feel of `curl`. It exercises every feature of the library.

```
Usage: fetch [OPTIONS] <URL>

Request:
  -X, --request  METHOD      HTTP method (GET POST PUT PATCH DELETE)
  -H, --header   "K: V"      Add request header (repeatable)
  -d, --data     BODY        Request body  (-d @file reads from a file)
  -j, --json     BODY        Body + Content-Type: application/json
  -F, --form     key=value   Form field (repeatable)
  -u, --user     user:pass   HTTP Basic auth

Response:
  -i, --include              Print response headers before body
  -I, --head                 HEAD request — headers only
  -o, --output   FILE        Save body to file
      --hex-dump             Print body as a hex dump
  -w, --write-out FORMAT     Print summary  (%{http_code} %{time_total_ms}
                             %{size_download} %{content_type} %{url})

Connection:
  -L, --location             Follow redirects (default: on)
      --no-location          Disable redirects
      --max-redirs   N       Max hops (default: 10)
  -k, --insecure             Skip TLS verification
      --timeout      MS      Timeout in milliseconds

Streaming:
      --sse                  Stream Server-Sent Events

Output:
      --base-url    URL      Use fetch::Client with this base URL
  -s, --silent               Suppress status / timing output
  -v, --verbose              Show request headers
      --color / --no-color   Force or disable ANSI color
      --timing               Print time + bytes after response
```

### CLI examples

```bash
# GET with pretty-printed JSON output
fetch https://jsonplaceholder.typicode.com/posts/1

# POST JSON
fetch -X POST -j '{"title":"hello","body":"world","userId":1}' \
    https://jsonplaceholder.typicode.com/posts

# POST a form
fetch -F username=alice -F password=secret https://httpbin.org/post

# Custom headers + verbose request/response dump
fetch -v -H "X-Request-ID: abc123" -H "Accept: application/json" \
    https://httpbin.org/headers

# Follow redirects, show response headers
fetch -i -L https://httpbin.org/redirect/3

# Ignore TLS errors
fetch -k https://self-signed.badssl.com/

# Timeout after 500 ms
fetch --timeout 500 https://httpbin.org/delay/5

# Save binary response to file
fetch -o photo.jpg https://httpbin.org/image/jpeg

# Hex dump of raw bytes (exercises Response::bytes())
fetch --hex-dump https://httpbin.org/bytes/64

# Use fetch::Client with a base URL (path appended automatically)
fetch --base-url https://jsonplaceholder.typicode.com /posts/1

# Stream Server-Sent Events
fetch --sse https://postman-echo.com/server-events/5

# -w write-out summary line  (like curl's --write-out)
fetch -s -w "%{http_code} %{time_total_ms}ms %{size_download}B\n" \
    https://httpbin.org/get

# HTTP Basic auth
fetch -u alice:secret https://httpbin.org/basic-auth/alice/secret

# Read body from file
fetch -X POST -H "Content-Type: application/json" \
    -d @payload.json https://httpbin.org/post
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.