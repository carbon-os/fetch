#include <fetch/client.hpp>
#include <fetch/error.hpp>
#include <fetch/detail/url.hpp>
#include <fetch/detail/socket.hpp>
#include <fetch/detail/tls.hpp>
#include <fetch/detail/http.hpp>
#include <array>

namespace fetch {

namespace {

template<typename Sock>
void send_all(Sock& s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        auto n = s.send(data.data() + sent, data.size() - sent);
        if (n <= 0) throw NetworkError("send failed");
        sent += static_cast<size_t>(n);
    }
}

template<typename Sock>
std::string recv_all(Sock& s) {
    std::string out;
    std::array<char, 8192> buf;
    ssize_t n;
    while ((n = s.recv(buf.data(), buf.size())) > 0)
        out.append(buf.data(), static_cast<size_t>(n));
    return out;
}

Response do_request(const std::string& method,
                    const std::string& url_str,
                    const Options& opts,
                    int hops = 0)
{
    auto url = detail::parse_url(url_str);
    auto req = detail::build_request(method, url, opts.headers, opts.body);
    std::string raw;

    if (url.is_https) {
        detail::TlsSocket sock(opts.verify_ssl);
        sock.connect(url.host, url.port, opts.timeout);
        send_all(sock, req);
        raw = recv_all(sock);
    } else {
        detail::TcpSocket sock;
        sock.connect(url.host, url.port, opts.timeout);
        send_all(sock, req);
        raw = recv_all(sock);
    }

    auto res = detail::parse_response(raw);

    if (opts.follow_redirects) {
        int s = res.status();
        if (s == 301 || s == 302 || s == 303 || s == 307 || s == 308) {
            if (hops >= opts.max_redirects)
                throw NetworkError("too many redirects");

            auto loc = res.header("location");
            if (loc.empty()) throw NetworkError("redirect missing Location header");

            if (loc.find("://") == std::string::npos) {
                std::string authority = url.host;
                if (( url.is_https && url.port != 443) ||
                    (!url.is_https && url.port != 80))
                    authority += ":" + std::to_string(url.port);
                if (loc.empty() || loc[0] != '/') loc = "/" + loc;
                loc = url.scheme + "://" + authority + loc;
            }

            auto next_method = (s == 303) ? "GET" : method;
            auto next_opts   = opts;
            if (s == 303) next_opts.body.clear();
            return do_request(next_method, loc, next_opts, hops + 1);
        }
    }

    return res;
}

// ---- Transparent Streaming Reader for SSE ----
template<typename Sock>
class SseStream {
    Sock& sock;
    std::string buf;
    bool eof = false;

    bool is_chunked = false;
    bool is_first_chunk = true;
    size_t chunk_left = 0;

    bool fill() {
        if (eof) return false;
        char temp[4096];
        ssize_t n = sock.recv(temp, sizeof(temp));
        if (n <= 0) { eof = true; return false; }
        buf.append(temp, static_cast<size_t>(n));
        return true;
    }

    bool get_raw_char(char& c) {
        if (buf.empty() && !fill()) return false;
        c = buf.front();
        buf.erase(0, 1);
        return true;
    }

    bool get_char(char& c) {
        if (!is_chunked) return get_raw_char(c);

        if (chunk_left == 0) {
            if (!is_first_chunk) {
                char cr, lf;
                if (!get_raw_char(cr) || !get_raw_char(lf)) return false;
            }
            is_first_chunk = false;

            std::string hex;
            char hx;
            while (get_raw_char(hx)) {
                if (hx == '\n') break;
                if (hx != '\r') hex.push_back(hx);
            }
            if (hex.empty()) return false;

            try { chunk_left = std::stoul(hex, nullptr, 16); }
            catch(...) { return false; }

            if (chunk_left == 0) return false;
        }

        if (!get_raw_char(c)) return false;
        chunk_left--;
        return true;
    }

public:
    SseStream(Sock& s) : sock(s) {}

    void set_chunked(bool c) { is_chunked = c; }

    bool read_http_line(std::string& line) {
        while (true) {
            auto pos = buf.find('\n');
            if (pos != std::string::npos) {
                line = buf.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                buf.erase(0, pos + 1);
                return true;
            }
            if (!fill()) return false;
        }
    }

    bool read_sse_line(std::string& line) {
        line.clear();
        char c;
        while (get_char(c)) {
            if (c == '\n') return true;
            if (c != '\r') line.push_back(c);
        }
        return !line.empty();
    }
};

void do_sse_request(const std::string& url_str, SseCallback cb, Options opts) {
    opts.headers["Accept"] = "text/event-stream";
    opts.headers["Cache-Control"] = "no-cache";
    opts.headers["Connection"] = "keep-alive";

    auto url = detail::parse_url(url_str);
    auto req = detail::build_request("GET", url, opts.headers, "");

    auto execute = [&](auto& sock) {
        sock.connect(url.host, url.port, opts.timeout);
        send_all(sock, req);

        SseStream<decltype(sock)> stream(sock);

        std::string line;
        int status = 0;
        bool chunked = false;

        if (stream.read_http_line(line)) {
            auto s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
            if (s1 != std::string::npos && s2 != std::string::npos)
                status = std::stoi(line.substr(s1 + 1, s2 - s1 - 1));
        }
        if (status != 200) throw HttpError(status, "SSE requires 200 OK");

        while (stream.read_http_line(line)) {
            if (line.empty()) break;
            std::string kl = line;
            std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
            if (kl.find("transfer-encoding: chunked") != std::string::npos)
                chunked = true;
        }

        stream.set_chunked(chunked);

        ServerEvent ev;
        bool has_data = false;

        while (stream.read_sse_line(line)) {
            if (line.empty()) {
                if (has_data) {
                    if (!cb(ev)) break;
                    ev = ServerEvent{};
                    has_data = false;
                }
                continue;
            }

            auto colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string field = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);

            if (field == "event") ev.event = value;
            else if (field == "data") {
                if (has_data) ev.data += "\n";
                ev.data += value;
                has_data = true;
            }
            else if (field == "id") ev.id = value;
            else if (field == "retry") { try { ev.retry = std::stoi(value); } catch(...) {} }
        }
    };

    if (url.is_https) {
        detail::TlsSocket sock(opts.verify_ssl); execute(sock);
    } else {
        detail::TcpSocket sock; execute(sock);
    }
}

} // anonymous namespace

// ---- Client ----

Client::Client(ClientOptions opts) : opts_(std::move(opts)) {}

Response Client::request(const std::string& method,
                         const std::string& path,
                         const Options& opts) const {
    Options merged = opts;
    for (auto& [k, v] : opts_.headers) merged.headers.try_emplace(k, v);
    merged.verify_ssl       = opts_.verify_ssl;
    merged.follow_redirects = opts_.follow_redirects;
    merged.max_redirects    = opts_.max_redirects;
    if (opts.timeout == Options{}.timeout) merged.timeout = opts_.timeout;
    return do_request(method, opts_.base_url + path, merged);
}

void Client::sse(const std::string& path, SseCallback cb, const Options& opts) const {
    Options merged = opts;
    for (auto& [k, v] : opts_.headers) merged.headers.try_emplace(k, v);
    merged.verify_ssl = opts_.verify_ssl;
    if (opts.timeout == Options{}.timeout) merged.timeout = opts_.timeout;
    do_sse_request(opts_.base_url + path, cb, merged);
}

Response Client::get  (const std::string& p, const Options& o) const { return request("GET",    p, o); }
Response Client::del  (const std::string& p, const Options& o) const { return request("DELETE", p, o); }

Response Client::post(const std::string& p, const std::string& body, const Options& o) const {
    Options x = o; x.body = body; return request("POST", p, x);
}
Response Client::post(const std::string& p, const Form& form, const Options& o) const {
    Options x = o;
    x.body = detail::form_encode(form.fields);
    x.headers["Content-Type"] = "application/x-www-form-urlencoded";
    return request("POST", p, x);
}
Response Client::put(const std::string& p, const std::string& body, const Options& o) const {
    Options x = o; x.body = body; return request("PUT", p, x);
}
Response Client::patch(const std::string& p, const std::string& body, const Options& o) const {
    Options x = o; x.body = body; return request("PATCH", p, x);
}

// ---- Free functions ----

Response get  (const std::string& url, const Options& o)                          { return do_request("GET",    url, o); }
Response del  (const std::string& url, const Options& o)                          { return do_request("DELETE", url, o); }
Response post (const std::string& url, const std::string& b, const Options& o)    { Options x=o; x.body=b; return do_request("POST",  url, x); }
Response put  (const std::string& url, const std::string& b, const Options& o)    { Options x=o; x.body=b; return do_request("PUT",   url, x); }
Response patch(const std::string& url, const std::string& b, const Options& o)    { Options x=o; x.body=b; return do_request("PATCH", url, x); }

Response post(const std::string& url, const Form& form, const Options& o) {
    Options x = o;
    x.body = detail::form_encode(form.fields);
    x.headers["Content-Type"] = "application/x-www-form-urlencoded";
    return do_request("POST", url, x);
}

void sse(const std::string& url, SseCallback cb, const Options& o) { do_sse_request(url, cb, o); }

} // namespace fetch