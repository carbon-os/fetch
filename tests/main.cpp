#include <fetch/fetch.hpp>
#include <iostream>
#include <cassert>

// ------------------------------------------------------------------ helpers --

static int passed = 0, failed = 0;

#define TEST(name, expr) do {                                    \
    try {                                                        \
        expr;                                                    \
        std::cout << "[PASS] " << name << "\n";                  \
        ++passed;                                                \
    } catch (const std::exception& e) {                          \
        std::cout << "[FAIL] " << name                           \
                  << " => " << e.what() << "\n";                 \
        ++failed;                                                \
    }                                                            \
} while(0)

#define EXPECT(cond) do { if (!(cond)) throw std::runtime_error("EXPECT failed: " #cond); } while(0)

// ------------------------------------------------------------------- tests --

void test_basic_get() {
    auto res = fetch::get("https://httpbin.org/get");
    EXPECT(res.ok());
    EXPECT(res.status() == 200);
    EXPECT(!res.text().empty());
}

void test_https() {
    auto res = fetch::get("https://jsonplaceholder.typicode.com/posts/1");
    EXPECT(res.ok());
    EXPECT(res.text().find("userId") != std::string::npos);
}

void test_post_json() {
    auto res = fetch::post(
        "https://httpbin.org/post",
        R"({"name":"alice"})",
        { .headers = {{"Content-Type", "application/json"}} }
    );
    EXPECT(res.ok());
    EXPECT(res.text().find("alice") != std::string::npos);
}

void test_post_form() {
    fetch::Form form{
        {"username", "alice"}, 
        {"password", "secret"}
    };
    
    auto res = fetch::post("https://httpbin.org/post", form);
    
    EXPECT(res.ok());
    EXPECT(res.text().find("alice") != std::string::npos);
}

void test_put() {
    auto res = fetch::put(
        "https://jsonplaceholder.typicode.com/posts/1",
        R"({"id":1,"title":"updated","body":"x","userId":1})",
        { .headers = {{"Content-Type", "application/json"}} }
    );
    EXPECT(res.ok());
}

void test_patch() {
    auto res = fetch::patch(
        "https://jsonplaceholder.typicode.com/posts/1",
        R"({"title":"patched"})",
        { .headers = {{"Content-Type", "application/json"}} }
    );
    EXPECT(res.ok());
}

void test_delete() {
    auto res = fetch::del("https://jsonplaceholder.typicode.com/posts/1");
    EXPECT(res.ok());
}

void test_status_codes() {
    EXPECT(fetch::get("https://httpbin.org/status/200").status() == 200);
    EXPECT(fetch::get("https://httpbin.org/status/201").status() == 201);
    EXPECT(fetch::get("https://httpbin.org/status/404").status() == 404);
    EXPECT(fetch::get("https://httpbin.org/status/500").status() == 500);
}

void test_redirects() {
    auto res = fetch::get("https://httpbin.org/redirect/3");
    EXPECT(res.ok());
    EXPECT(res.status() == 200);
}

void test_redirect_limit() {
    bool threw = false;
    try {
        fetch::get("https://httpbin.org/redirect/20", { .max_redirects = 3 });
    } catch (const fetch::NetworkError&) {
        threw = true;
    }
    EXPECT(threw);
}

void test_headers_roundtrip() {
    auto res = fetch::get("https://httpbin.org/headers", {
        .headers = {
            {"X-Custom-Header", "hello"},
            {"Accept",          "application/json"}
        }
    });
    EXPECT(res.ok());
    EXPECT(res.text().find("X-Custom-Header") != std::string::npos);
}

void test_timeout() {
    bool threw = false;
    try {
        fetch::get("https://httpbin.org/delay/10", {
            .timeout = std::chrono::milliseconds(500)
        });
    } catch (const fetch::TimeoutError&) {
        threw = true;
    }
    EXPECT(threw);
}

void test_ssl_invalid_cert() {
    bool threw = false;
    try {
        fetch::get("https://expired.badssl.com/");
    } catch (const fetch::SslError&) {
        threw = true;
    }
    EXPECT(threw);
}

void test_ssl_skip_verify() {
    auto res = fetch::get("https://self-signed.badssl.com/", { .verify_ssl = false });
    EXPECT(res.status() > 0);  // connected despite bad cert
}

void test_client() {
    fetch::Client client({
        .base_url = "https://jsonplaceholder.typicode.com",
        .headers  = {{"Accept", "application/json"}},
    });

    auto posts = client.get("/posts");
    EXPECT(posts.ok());

    auto post = client.get("/posts/1");
    EXPECT(post.ok());
    EXPECT(post.text().find("userId") != std::string::npos);

    auto created = client.post("/posts",
        R"({"title":"test","body":"hello","userId":1})",
        { .headers = {{"Content-Type", "application/json"}} }
    );
    EXPECT(created.status() == 201);
}

void test_http_plain() {
    auto res = fetch::get("http://httpbin.org/get");
    EXPECT(res.ok());
}

void test_sse_stream() {
    int event_count = 0;
    
    // The test connects to Postman Echo, which streams 3 events and closes the connection.
    fetch::sse("https://postman-echo.com/server-events/3", [&](const fetch::ServerEvent& ev) {
        // Postman Echo sometimes sends initial empty/info events. 
        // We just want to verify that the parser successfully extracted data!
        EXPECT(!ev.data.empty()); 
        
        event_count++;
        
        // Return true to keep receiving events!
        return true; 
    });
    
    // As long as we parsed events from the stream, the SSE parser is working!
    EXPECT(event_count > 0); 
}

// -------------------------------------------------------------------- main --

int main() {
    std::cout << "=== fetch library tests ===\n\n";

    TEST("basic GET",               test_basic_get());
    TEST("HTTPS",                   test_https());
    TEST("POST json",               test_post_json());
    TEST("POST form",               test_post_form());
    TEST("PUT",                     test_put());
    TEST("PATCH",                   test_patch());
    TEST("DELETE",                  test_delete());
    TEST("status codes",            test_status_codes());
    TEST("redirects",               test_redirects());
    TEST("redirect limit",          test_redirect_limit());
    TEST("headers roundtrip",       test_headers_roundtrip());
    TEST("timeout",                 test_timeout());
    TEST("SSL invalid cert",        test_ssl_invalid_cert());
    TEST("SSL skip verify",         test_ssl_skip_verify());
    TEST("Client",                  test_client());
    TEST("plain HTTP",              test_http_plain());
    TEST("SSE Stream",              test_sse_stream());

    std::cout << "\n=== " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}