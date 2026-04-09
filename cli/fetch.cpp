// cli/fetch.cpp — curl-like CLI showcasing the fetch library
//
//  fetch [OPTIONS] <URL>
//  fetch --help

#include <fetch/fetch.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#  define isatty _isatty
#  define fileno _fileno
#else
#  include <unistd.h>
#endif

// ════════════════════════════════════════════════════════════════ Color ══════

namespace color {
    static bool on = false;
    void init(bool force) { on = force || static_cast<bool>(isatty(fileno(stdout))); }

    const char* R    () { return on ? "\033[0m"  : ""; }   // reset
    const char* bold () { return on ? "\033[1m"  : ""; }
    const char* dim  () { return on ? "\033[2m"  : ""; }
    const char* red  () { return on ? "\033[31m" : ""; }
    const char* green() { return on ? "\033[32m" : ""; }
    const char* yel  () { return on ? "\033[33m" : ""; }
    const char* blue () { return on ? "\033[34m" : ""; }
    const char* mag  () { return on ? "\033[35m" : ""; }
    const char* cyan () { return on ? "\033[36m" : ""; }

    const char* for_status(int s) {
        if (!on) return "";
        if (s >= 200 && s < 300) return "\033[32m";
        if (s >= 300 && s < 400) return "\033[33m";
        if (s >= 400 && s < 500) return "\033[31m";
        if (s >= 500)             return "\033[35m";
        return "";
    }
} // namespace color

// ════════════════════════════════════════════════════════════ JSON Pretty ════

namespace json {

// Indent a compact JSON string.  Not a full validator — good enough for output.
std::string pretty(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 2);
    int  depth  = 0;
    bool in_str = false, esc = false;

    auto nl = [&]() {
        out += '\n';
        for (int d = 0; d < depth * 2; ++d) out += ' ';
    };

    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];

        if (esc)              { out += c; esc = false; continue; }
        if (c == '\\' && in_str) { out += c; esc = true;  continue; }
        if (c == '"')         { in_str = !in_str; out += c; continue; }
        if (in_str)           { out += c; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) continue;

        switch (c) {
        case '{': case '[': {
            out += c;
            // stay on one line for empty objects / arrays
            size_t j = i + 1;
            while (j < in.size() && std::isspace(static_cast<unsigned char>(in[j]))) ++j;
            if (j < in.size() && (in[j] == '}' || in[j] == ']')) break;
            ++depth; nl();
            break;
        }
        case '}': case ']': --depth; nl(); out += c;   break;
        case ',':                          out += c; nl(); break;
        case ':':                          out += ": ";   break;
        default:                           out += c;
        }
    }
    return out;
}

bool detect(const std::string& body, const std::string& ct) {
    if (ct.find("json") != std::string::npos) return true;
    size_t i = 0;
    while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
    return i < body.size() && (body[i] == '{' || body[i] == '[');
}

} // namespace json

// ══════════════════════════════════════════════════════════════ Hex Dump ═════

// Showcases Response::bytes() — prints classic xxd-style hex dump.
static void hex_dump(const std::vector<std::byte>& data) {
    for (size_t i = 0; i < data.size(); i += 16) {
        // offset
        std::cout << color::dim()
                  << std::hex << std::setw(8) << std::setfill('0') << i
                  << color::R() << "  ";
        // hex bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < data.size())
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(data[i + j]) << ' ';
            else
                std::cout << "   ";
            if (j == 7) std::cout << ' ';
        }
        // ascii column
        std::cout << " |";
        for (size_t j = 0; j < 16 && i + j < data.size(); ++j) {
            auto b = static_cast<unsigned char>(data[i + j]);
            std::cout << (std::isprint(b) ? static_cast<char>(b) : '.');
        }
        std::cout << "|\n";
    }
    std::cout << std::dec;  // restore
}

// ══════════════════════════════════════════════════════════════ CLI Args ═════

struct Args {
    std::string              url;
    std::string              method;            // empty = auto-detect
    std::vector<std::string> raw_headers;       // "Key: Value"
    std::string              body;
    bool                     body_is_form = false;
    std::vector<std::string> form_fields;       // "key=value"
    std::string              output_file;
    bool include_headers = false;               // -i
    bool follow_redir    = true;                // -L / --no-location
    int  max_redirs      = 10;
    bool insecure        = false;               // -k
    long timeout_ms      = 30'000;
    bool sse             = false;               // --sse
    bool silent          = false;               // -s
    bool verbose         = false;               // -v
    bool color_force     = false;               // --color
    bool no_color        = false;               // --no-color
    bool timing          = false;               // --timing
    bool hex_dump_mode   = false;               // --hex-dump  (uses bytes())
    std::string base_url;                       // --base-url  (triggers Client)
    std::string write_out;                      // -w
};

// ─────────────────────────────────────────────────────────────── usage ──────

static void usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [OPTIONS] <URL>\n"
           "\n"
           "A curl-like HTTP client built on the fetch library.\n"
           "\n"
           "Request:\n"
           "  -X, --request  METHOD      HTTP method (GET POST PUT PATCH DELETE, default: GET)\n"
           "  -H, --header   \"K: V\"      Add request header (repeatable)\n"
           "  -d, --data     BODY        Set request body  (-d @file reads from file)\n"
           "  -j, --json     BODY        Body + Content-Type: application/json\n"
           "  -F, --form     key=value   Form field (repeatable); sets form Content-Type\n"
           "  -u, --user     user:pass   HTTP Basic authentication\n"
           "\n"
           "Response:\n"
           "  -i, --include              Print response headers before body\n"
           "  -I, --head                 HEAD request — show headers only\n"
           "  -o, --output   FILE        Save body to FILE instead of stdout\n"
           "      --hex-dump             Print body as hex dump  (uses Response::bytes())\n"
           "  -w, --write-out FORMAT     Print summary line after response\n"
           "                             Tokens: %{http_code}  %{time_total_ms}\n"
           "                                     %{size_download}  %{content_type}  %{url}\n"
           "\n"
           "Connection:\n"
           "  -L, --location             Follow redirects (default: on)\n"
           "      --no-location          Disable redirect following\n"
           "      --max-redirs   N       Max redirect hops (default: 10)\n"
           "  -k, --insecure             Skip TLS certificate verification\n"
           "      --timeout      MS      Timeout in milliseconds (default: 30000)\n"
           "\n"
           "Streaming:\n"
           "      --sse                  Connect and stream Server-Sent Events\n"
           "\n"
           "Output:\n"
           "      --base-url    URL      Use fetch::Client with this base URL\n"
           "  -s, --silent               Suppress informational stderr output\n"
           "  -v, --verbose              Show request headers (+ client info)\n"
           "      --color                Force ANSI color output\n"
           "      --no-color             Disable ANSI color output\n"
           "      --timing               Print timing + size summary\n"
           "\n"
           "Examples:\n"
        << "  " << prog << " https://httpbin.org/get\n"
        << "  " << prog << " -X POST -j '{\"name\":\"alice\"}' https://httpbin.org/post\n"
        << "  " << prog << " -F username=alice -F password=s3cr3t https://httpbin.org/post\n"
        << "  " << prog << " -i -v https://httpbin.org/redirect/2\n"
        << "  " << prog << " -k https://self-signed.badssl.com/\n"
        << "  " << prog << " --timeout 500 https://httpbin.org/delay/5\n"
        << "  " << prog << " --sse https://postman-echo.com/server-events/5\n"
        << "  " << prog << " --base-url https://jsonplaceholder.typicode.com /posts/1\n"
        << "  " << prog << " --hex-dump https://httpbin.org/bytes/64\n"
        << "  " << prog << " -w \"%{http_code} %{time_total_ms}ms\\n\" https://httpbin.org/get\n";
}

// ─────────────────────────────────────────────────────────── base64 ─────────

static std::string b64(const std::string& s) {
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned buf = 0; int bits = 0;
    for (unsigned char c : s) {
        buf = (buf << 8) | c; bits += 8;
        while (bits >= 6) { bits -= 6; out += T[(buf >> bits) & 0x3f]; }
    }
    if (bits) out += T[(buf << (6 - bits)) & 0x3f];
    while (out.size() % 4) out += '=';
    return out;
}

// ─────────────────────────────────────────────────────────── file read ───────

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);
    return { std::istreambuf_iterator<char>(f), {} };
}

// ─────────────────────────────────────────────────────────── arg parse ───────

static Args parse_args(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); exit(1); }
    Args a;

    auto next = [&](int& i) -> std::string {
        if (++i >= argc) {
            std::cerr << "error: " << argv[i - 1] << " requires an argument\n";
            exit(1);
        }
        return argv[i];
    };

    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];

        if      (s == "-h" || s == "--help")     { usage(argv[0]); exit(0); }
        else if (s == "-X" || s == "--request")  a.method = next(i);
        else if (s == "-H" || s == "--header")   a.raw_headers.push_back(next(i));
        else if (s == "-d" || s == "--data") {
            std::string v = next(i);
            a.body = (!v.empty() && v[0] == '@') ? slurp(v.substr(1)) : v;
        }
        else if (s == "-j" || s == "--json") {
            std::string v = next(i);
            a.body = (!v.empty() && v[0] == '@') ? slurp(v.substr(1)) : v;
            a.raw_headers.push_back("Content-Type: application/json");
        }
        else if (s == "-F" || s == "--form") {
            a.form_fields.push_back(next(i));
            a.body_is_form = true;
        }
        else if (s == "-u" || s == "--user")
            a.raw_headers.push_back("Authorization: Basic " + b64(next(i)));
        else if (s == "-o" || s == "--output")   a.output_file    = next(i);
        else if (s == "-i" || s == "--include")  a.include_headers = true;
        else if (s == "-I" || s == "--head")     a.method          = "HEAD";
        else if (s == "-L" || s == "--location") a.follow_redir    = true;
        else if (s == "--no-location")           a.follow_redir    = false;
        else if (s == "--max-redirs")            a.max_redirs      = std::stoi(next(i));
        else if (s == "-k" || s == "--insecure") a.insecure        = true;
        else if (s == "--timeout")               a.timeout_ms      = std::stol(next(i));
        else if (s == "--sse")                   a.sse             = true;
        else if (s == "-s" || s == "--silent")   a.silent          = true;
        else if (s == "-v" || s == "--verbose")  a.verbose         = true;
        else if (s == "--color")                 a.color_force     = true;
        else if (s == "--no-color")              a.no_color        = true;
        else if (s == "--timing")                a.timing          = true;
        else if (s == "--hex-dump")              a.hex_dump_mode   = true;
        else if (s == "--base-url")              a.base_url        = next(i);
        else if (s == "-w" || s == "--write-out") a.write_out      = next(i);
        else if (s[0] != '-')                    a.url             = s;
        else { std::cerr << "error: unknown option: " << s << "\n"; exit(1); }
    }

    if (a.url.empty()) {
        std::cerr << "error: no URL specified\n";
        usage(argv[0]);
        exit(1);
    }
    return a;
}

// ─────────────────────────── build fetch::Options + fetch::Form ─────────────

static void populate(const Args& a, fetch::Options& opts, fetch::Form& form) {
    opts.follow_redirects = a.follow_redir;
    opts.max_redirects    = a.max_redirs;
    opts.verify_ssl       = !a.insecure;
    opts.timeout          = std::chrono::milliseconds(a.timeout_ms);
    opts.body             = a.body;

    for (const auto& h : a.raw_headers) {
        auto c = h.find(':');
        if (c == std::string::npos) continue;
        std::string k = h.substr(0, c);
        std::string v = h.substr(c + 1);
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        opts.headers[k] = v;
    }

    for (const auto& f : a.form_fields) {
        auto eq = f.find('=');
        if (eq != std::string::npos)
            form.fields[f.substr(0, eq)] = f.substr(eq + 1);
    }
}

// ═══════════════════════════════════════════════════════════ Output helpers ══

static void print_req_verbose(const std::string& method,
                               const std::string& url,
                               const fetch::Options& opts) {
    std::cerr << color::bold() << color::cyan()
              << "> " << method << " " << url
              << color::R() << "\n";
    for (const auto& [k, v] : opts.headers)
        std::cerr << color::dim() << "> " << k << ": " << v << color::R() << "\n";
    if (!opts.body.empty())
        std::cerr << color::dim() << "> Body: " << opts.body.size()
                  << " bytes" << color::R() << "\n";
    std::cerr << ">\n";
}

static void print_resp_headers(const fetch::Response& res) {
    std::cout << color::bold() << color::for_status(res.status())
              << "< HTTP/1.1 " << res.status() << " " << res.status_text()
              << color::R() << "\n";
    for (const auto& [k, v] : res.headers())
        std::cout << color::dim() << "< " << k << ": " << v << color::R() << "\n";
    std::cout << "<\n";
}

static void print_status_banner(const fetch::Response& res) {
    std::cerr << color::bold() << color::for_status(res.status())
              << "HTTP " << res.status() << " " << res.status_text()
              << color::R();
    auto ct = res.header("Content-Type");
    if (!ct.empty())
        std::cerr << "  " << color::dim() << ct << color::R();
    std::cerr << "\n";
}

static void write_body(const fetch::Response& res, const Args& a) {
    // Hex-dump mode: showcases Response::bytes()
    if (a.hex_dump_mode) {
        hex_dump(res.bytes());
        return;
    }

    const auto& body = res.text();
    if (body.empty()) return;

    // Save to file
    if (!a.output_file.empty()) {
        std::ofstream f(a.output_file, std::ios::binary);
        if (!f) throw std::runtime_error("cannot write: " + a.output_file);
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!a.silent)
            std::cerr << color::dim() << "Wrote " << body.size()
                      << " bytes → " << a.output_file << color::R() << "\n";
        return;
    }

    // Pretty-print JSON; pass everything else through as-is
    if (json::detect(body, res.header("Content-Type")))
        std::cout << json::pretty(body) << "\n";
    else {
        std::cout << body;
        if (body.back() != '\n') std::cout << '\n';
    }
}

static void do_write_out(const std::string& fmt, const fetch::Response& res,
                          long elapsed_ms, const std::string& url) {
    std::string out = fmt;
    struct Sub { const char* tok; std::string val; };
    Sub subs[] = {
        { "%{http_code}",      std::to_string(res.status())         },
        { "%{time_total_ms}",  std::to_string(elapsed_ms)           },
        { "%{size_download}",  std::to_string(res.text().size())     },
        { "%{content_type}",   res.header("Content-Type")           },
        { "%{url}",            url                                   },
    };
    for (auto& [tok, val] : subs) {
        size_t pos;
        while ((pos = out.find(tok)) != std::string::npos)
            out.replace(pos, std::strlen(tok), val);
    }
    // Handle common escape sequences
    auto replace_esc = [&](const char* esc, char ch) {
        size_t pos;
        while ((pos = out.find(esc)) != std::string::npos)
            out.replace(pos, 2, 1, ch);
    };
    replace_esc("\\n", '\n');
    replace_esc("\\t", '\t');
    std::cout << out;
}

// ══════════════════════════════════════════════════════════════ SSE mode ═════

// Showcases fetch::sse() + SseCallback + ServerEvent fields
static void run_sse(const std::string& url, fetch::Options& opts, const Args& a) {
    if (!a.silent)
        std::cerr << color::bold() << color::mag()
                  << "⟳  SSE  " << url << color::R() << "\n\n";

    int n = 0;
    fetch::sse(url, [&](const fetch::ServerEvent& ev) -> bool {
        ++n;
        // ── event header ────────────────────────────────────────────────
        std::cout << color::bold() << color::cyan() << "── event #" << n;
        if (!ev.id.empty())          std::cout << "  id=" << ev.id;
        if (ev.event != "message")   std::cout << "  type=" << color::yel()
                                               << ev.event << color::cyan();
        std::cout << color::R() << "\n";

        // ── payload ─────────────────────────────────────────────────────
        if (json::detect(ev.data, ""))
            std::cout << json::pretty(ev.data) << "\n\n";
        else
            std::cout << ev.data << "\n\n";

        if (ev.retry > 0)
            std::cout << color::dim() << "(retry hint: " << ev.retry
                      << " ms)" << color::R() << "\n";

        return true; // keep the stream alive
    }, opts);

    if (!a.silent)
        std::cerr << color::dim() << "Stream closed — "
                  << n << " event(s) received." << color::R() << "\n";
}

// ══════════════════════════════════════════════════════════════════ main ═════

int main(int argc, char* argv[]) {
    Args a = parse_args(argc, argv);

    if (!a.no_color) color::init(a.color_force);

    // Build shared options and form data
    fetch::Options opts;
    fetch::Form    form({});
    populate(a, opts, form);

    // Auto-detect method when not explicitly set
    std::string method = a.method;
    if (method.empty())
        method = (a.body_is_form || !opts.body.empty()) ? "POST" : "GET";

    if (a.verbose) print_req_verbose(method, a.url, opts);

    const auto t0 = std::chrono::steady_clock::now();

    try {
        // ── SSE streaming ─────────────────────────────────────────────────
        if (a.sse) {
            run_sse(a.url, opts, a);
            return 0;
        }

        // ── Regular HTTP ─────────────────────────────────────────────────
        fetch::Response res;

        if (!a.base_url.empty()) {
            // ┌──────────────────────────────────────────────────────────┐
            // │  Client mode — demonstrates fetch::Client                │
            // │  Base URL + shared headers set once on the client;       │
            // │  the path (a.url) is appended to every request.          │
            // └──────────────────────────────────────────────────────────┘
            fetch::ClientOptions co;
            co.base_url         = a.base_url;
            co.headers          = opts.headers;   // shared on every request
            co.verify_ssl       = opts.verify_ssl;
            co.follow_redirects = opts.follow_redirects;
            co.max_redirects    = opts.max_redirects;
            co.timeout          = opts.timeout;

            fetch::Client client(co);

            if (a.verbose)
                std::cerr << color::dim() << "  Client base_url=" << a.base_url
                          << color::R() << "\n";

            res = a.body_is_form
                ? client.post(a.url, form)
                : client.request(method, a.url, opts);

        } else {
            // ┌──────────────────────────────────────────────────────────┐
            // │  Free-function mode — the fetch::get/post/… one-liners  │
            // └──────────────────────────────────────────────────────────┘
            if      (a.body_is_form)   res = fetch::post (a.url, form,       opts);
            else if (method == "GET")  res = fetch::get  (a.url,             opts);
            else if (method == "POST") res = fetch::post (a.url, opts.body,  opts);
            else if (method == "PUT")  res = fetch::put  (a.url, opts.body,  opts);
            else if (method == "PATCH")res = fetch::patch(a.url, opts.body,  opts);
            else if (method == "DELETE")res = fetch::del (a.url,             opts);
            else {
                // Arbitrary method (HEAD, OPTIONS, …) via Client
                fetch::Client c({ .base_url = "" });
                res = c.request(method, a.url, opts);
            }
        }

        const auto t1      = std::chrono::steady_clock::now();
        const long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        // ── Render ────────────────────────────────────────────────────────
        if (!a.silent)
            print_status_banner(res);

        if (a.include_headers || method == "HEAD")
            print_resp_headers(res);

        if (method != "HEAD")
            write_body(res, a);

        if (a.timing && !a.silent)
            std::cerr << "\n" << color::dim()
                      << "Time: " << elapsed << " ms   "
                      << "Downloaded: " << res.text().size() << " bytes"
                      << color::R() << "\n";

        if (!a.write_out.empty())
            do_write_out(a.write_out, res, elapsed, a.url);

        // Exit 0 on 2xx, 1 on any HTTP error status (mirrors curl)
        return res.ok() ? 0 : 1;

    } catch (const fetch::TimeoutError&  e) {
        std::cerr << color::bold() << color::red()
                  << "Timeout: "       << e.what() << color::R() << "\n"; return 28;
    } catch (const fetch::SslError&      e) {
        std::cerr << color::bold() << color::red()
                  << "TLS error: "     << e.what() << color::R() << "\n"; return 35;
    } catch (const fetch::NetworkError&  e) {
        std::cerr << color::bold() << color::red()
                  << "Network error: " << e.what() << color::R() << "\n"; return  6;
    } catch (const fetch::HttpError&     e) {
        std::cerr << color::bold() << color::red()
                  << "HTTP error: "    << e.what() << color::R() << "\n"; return 22;
    } catch (const std::exception&       e) {
        std::cerr << color::bold() << color::red()
                  << "Error: "         << e.what() << color::R() << "\n"; return  1;
    }
}