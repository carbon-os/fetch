#include <fetch/detail/url.hpp>
#include <fetch/error.hpp>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace fetch::detail {

ParsedUrl parse_url(std::string_view url) {
    ParsedUrl result;

    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos)
        throw fetch::NetworkError("invalid URL: missing scheme");

    result.scheme   = std::string(url.substr(0, scheme_end));
    result.is_https = (result.scheme == "https");

    auto rest = url.substr(scheme_end + 3);

    auto path_start = rest.find('/');
    std::string_view authority;

    if (path_start == std::string_view::npos) {
        authority     = rest;
        result.target = "/";
    } else {
        authority     = rest.substr(0, path_start);
        result.target = std::string(rest.substr(path_start));
    }

    auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        result.host = std::string(authority.substr(0, colon));
        result.port = static_cast<uint16_t>(
            std::stoi(std::string(authority.substr(colon + 1))));
    } else {
        result.host = std::string(authority);
        result.port = result.is_https ? 443 : 80;
    }

    return result;
}

std::string url_encode(std::string_view s) {
    std::ostringstream out;
    for (char raw : s) {
        auto c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << raw;
        else
            out << '%' << std::uppercase << std::hex
                << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return out.str();
}

} // namespace fetch::detail