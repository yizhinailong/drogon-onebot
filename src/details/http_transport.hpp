/**
 * @file http_transport.hpp
 * @brief Declare the transport that calls actions on the HTTP API of a OneBot implementation.
 */
#pragma once

#include <string>

#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include "details/transport.hpp"
#include "drogon_onebot/error.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Call actions on the HTTP API of one OneBot v11 implementation.
     *
     * Each request gets its own HTTP client on the calling thread's event loop. drogon
     * allows one in-flight request per client, so a shared client would serialize them;
     * a client on the caller's loop also makes the coroutine resume on the caller's thread.
     */
    class HttpTransport final : public Transport {
    private:
        OneBotConfig m_config;     ///< Endpoint settings.
        std::string m_host;        ///< Scheme, host, and port of `base_url`, for `HttpClient::newHttpClient()`.
        std::string m_path_prefix; ///< Path part of `base_url`, e.g. `/onebot`; empty without one.

    public:
        /**
         * @brief Store the endpoint settings and split `config.base_url`.
         * @param config Endpoint settings; `access_token` may be empty for endpoints without auth.
         */
        explicit HttpTransport(OneBotConfig config);

        /**
         * @brief POST an action's parameters and await the `data` of its response envelope.
         * @param action Action name, e.g. `send_private_msg`; the request goes to `<base_url>/<action>`.
         * @param params Action parameters, sent as the JSON request body.
         * @param timeout Seconds to wait for the response; 0 waits forever.
         * @return Task yielding the envelope's `data` when `status` is `ok` or `async`, or why the action failed.
         * @note The caller keeps this object alive until the task completes.
         */
        [[nodiscard]] auto Call(
            std::string action,
            Json::Value params,
            double timeout
        ) -> drogon::Task<Result<Json::Value>> override;
    };

} // namespace drogon_onebot::details
