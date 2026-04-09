#pragma once
#include <string>
#include <string_view>
#include <map>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace fetch {

// Case-insensitive comparator for header keys
struct CaseInsensitiveLess {
    // This tells std::map it's safe to use std::string_view for lookups
    using is_transparent = void;

    bool operator()(std::string_view a, std::string_view b) const {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](unsigned char c1, unsigned char c2) {
                return std::tolower(c1) < std::tolower(c2);
            }
        );
    }
};

// Use the comparator instead of std::less<>
using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

struct Options {
    std::string  method           = "GET";
    Headers      headers;
    std::string  body;
    std::chrono::milliseconds timeout{30'000};
    bool         follow_redirects = true;
    int          max_redirects    = 10;
    bool         verify_ssl       = true;
};

struct Form {
    std::map<std::string, std::string> fields;
    Form(std::initializer_list<std::pair<const std::string, std::string>> init)
        : fields(init) {}
};

} // namespace fetch