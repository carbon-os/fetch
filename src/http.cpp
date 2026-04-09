#include <fetch/detail/http.hpp>
#include <fetch/error.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>

namespace fetch::detail {

std::string build_request(const std::string& method,
                          const ParsedUrl& url,
                          const fetch::Headers& headers,
                          const std::string& body) {
    std::ostringstream r;
    r << method << ' ' << url.target << " HTTP/1.1\r\n"
      << "Host: "       << url.host  << "\r\n"
      << "Connection: close\r\n";

    bool has_content_length = false;
    for (auto& [k, v] : headers) {
        std::string kl = k;
        std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
        if (kl == "content-length") has_content_length = true;
        r << k << ": " << v << "\r\n";
    }

    if (!has_content_length && (method == "POST" || method == "PUT" || method == "PATCH" || !body.empty())) {
        r << "Content-Length: " << body.size() << "\r\n";
    }

    r << "\r\n" << body;
    return r.str();
}

static std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return std::string(s.substr(a, b - a));
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string decode_chunked(const std::string& data) {
    std::string out;
    size_t pos = 0;
    while (pos < data.size()) {
        auto crlf = data.find("\r\n", pos);
        if (crlf == std::string::npos) break;

        std::string sz = data.substr(pos, crlf - pos);
        if (auto semi = sz.find(';'); semi != std::string::npos) sz.resize(semi);
        sz = trim(sz);
        if (sz.empty()) break;

        size_t chunk = 0;
        try { chunk = std::stoul(sz, nullptr, 16); } catch (...) { break; }

        if (chunk == 0) break;
        pos = crlf + 2;

        if (pos >= data.size()) break;
        size_t available = data.size() - pos;
        size_t copy_len = (chunk < available) ? chunk : available;

        out.append(data, pos, copy_len);
        if (chunk > SIZE_MAX - pos - 2) break;
        pos += chunk + 2;
    }
    return out;
}

fetch::Response parse_response(const std::string& raw_in) {
    try {
        std::string raw = raw_in;
        while (true) {
            auto sep = raw.find("\r\n\r\n");
            if (sep == std::string::npos)
                throw fetch::NetworkError("malformed HTTP response");

            std::istringstream hstream(raw.substr(0, sep));
            std::string body = raw.substr(sep + 4);

            std::string status_line;
            std::getline(hstream, status_line);
            if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();

            std::istringstream sl(status_line);
            std::string ver, code;
            sl >> ver >> code;
            std::string status_msg; std::getline(sl, status_msg);

            int status = 0;
            if (!code.empty()) {
                try { status = std::stoi(code); } catch (...) {}
            }

            if (status == 100) {
                raw = body;
                continue;
            }

            fetch::Headers headers;
            std::string line;
            while (std::getline(hstream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) break;
                auto c = line.find(':');
                if (c != std::string::npos && c < line.size()) {
                    headers[trim(line.substr(0, c))] = trim(line.substr(c + 1));
                }
            }

            bool is_chunked = false;
            for (auto& [k, v] : headers) {
                if (lower(k) == "transfer-encoding" && lower(v).find("chunked") != std::string::npos) {
                    is_chunked = true;
                    break;
                }
            }
            if (is_chunked) body = decode_chunked(body);

            return fetch::Response(status, trim(status_msg), std::move(headers), std::move(body));
        }
    } catch (const std::exception& e) {
        throw fetch::NetworkError("Parse failed: malformed HTTP response");
    }
}

std::string form_encode(const std::map<std::string, std::string>& fields) {
    auto encode = [](std::string_view s) {
        std::ostringstream out;
        for (char raw : s) {
            if (raw == ' ') {
                out << '+';
            } else {
                auto c = static_cast<unsigned char>(raw);
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                    out << raw;
                else
                    out << '%' << std::uppercase << std::hex
                        << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            }
        }
        return out.str();
    };

    std::ostringstream out;
    bool first = true;
    for (auto& [k, v] : fields) {
        if (!first) out << '&';
        out << encode(k) << '=' << encode(v);
        first = false;
    }
    return out.str();
}

} // namespace fetch::detail