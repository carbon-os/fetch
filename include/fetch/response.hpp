#pragma once
#include "request.hpp"
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>

namespace fetch {

class Response {
public:
    Response() = default;
    Response(int status, std::string status_text,
             Headers headers, std::string body)
        : status_(status)
        , status_text_(std::move(status_text))
        , headers_(std::move(headers))
        , body_(std::move(body))
    {}

    int                status()      const { return status_; }
    const std::string& status_text() const { return status_text_; }
    bool               ok()          const { return status_ >= 200 && status_ < 300; }
    const Headers&     headers()     const { return headers_; }
    const std::string& text()        const { return body_; }

    std::string header(std::string_view name) const {
        auto it = headers_.find(name);
        return it != headers_.end() ? it->second : "";
    }

    std::vector<std::byte> bytes() const {
        std::vector<std::byte> out(body_.size());
        std::transform(body_.begin(), body_.end(), out.begin(),
            [](char c) { return static_cast<std::byte>(c); });
        return out;
    }

private:
    int         status_      = 0;
    std::string status_text_;
    Headers     headers_;
    std::string body_;
};

} // namespace fetch