/**
 * @file http_trace.hpp
 * @brief Observe bounded HTTP request attempts without exposing authorization headers.
 */
#pragma once

#include <functional>
#include <string>

namespace drogon_onebot {

    /**
     * @brief Owned diagnostic data for one HTTP attempt; headers are intentionally excluded.
     */
    struct HttpTrace {
        std::string method;        ///< HTTP method.
        std::string url;           ///< Endpoint and request target.
        int status_code{};         ///< HTTP status, or zero on transport failure.
        double elapsed_ms{};       ///< Monotonic attempt duration in milliseconds.
        std::string request_body;  ///< At most 64 KiB of textual request data.
        std::string response_body; ///< At most 64 KiB of textual response data.
        std::string error;         ///< Transport error summary, empty when a response arrived.
        bool request_truncated{};  ///< Request exceeded the retained prefix.
        bool response_truncated{}; ///< Response exceeded the retained prefix.
    };

    /**
     * @brief Optional observer called after each attempt on its calling event loop.
     * @note Do not block. Concurrent requests may call it concurrently. Exceptions are ignored.
     * Payloads may contain application data; consumers must apply their own content redaction.
     */
    using HttpTraceCallback = std::function<void(HttpTrace const&)>;

} // namespace drogon_onebot
