#pragma once
#include "../request.hpp"
#include "../response.hpp"
#include "url.hpp"
#include <map>

namespace fetch::detail {

std::string build_request(const std::string& method,
                          const ParsedUrl&   url,
                          const fetch::Headers& headers,
                          const std::string& body);

fetch::Response parse_response(const std::string& raw);

std::string form_encode(const std::map<std::string, std::string>& fields);

} // namespace fetch::detail