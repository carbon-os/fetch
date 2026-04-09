#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace fetch::detail {

struct ParsedUrl {
    std::string scheme;    // "http" | "https"
    std::string host;
    uint16_t    port  = 80;
    std::string target;    // path + query
    bool        is_https = false;
};

ParsedUrl   parse_url  (std::string_view url);
std::string url_encode (std::string_view s);

} // namespace fetch::detail