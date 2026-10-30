/**
 * @file transport.hpp
 * @brief Declare the transport interface that the copies of one `OneBot` share, and the envelope helpers of its implementations.
 */
#pragma once

#include <string>

#include <drogon/utils/coroutine.h>
#include <json/value.h>
#include <trantor/net/EventLoop.h>

#include "drogon_onebot/error.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Call actions on one OneBot v11 implementation.
     *
     * `HttpTransport` posts each call to the HTTP API; `ReverseWebSocket` sends it over the
     * connection that the implementation opened. Both may be called from any thread.
     */
    class Transport {
    public:
        virtual ~Transport() = default;

        /**
         * @brief Send an action's parameters and await the `data` of its response envelope.
         * @param action Action name, e.g. `send_private_msg`.
         * @param params Action parameters.
         * @param timeout Seconds to wait for the response; 0 waits forever.
         * @return Task yielding the envelope's `data` when `status` is `ok` or `async`, or why the action failed.
         * @note The caller keeps this object alive until the task completes. The task resumes
         * on the calling thread's event loop, or on the main loop when the thread runs none.
         */
        [[nodiscard]] virtual auto Call(
            std::string action,
            Json::Value params,
            double timeout
        ) -> drogon::Task<Result<Json::Value>> = 0;
    };

    /**
     * @brief Pick the event loop that a call runs on and resumes its caller on.
     * @return The calling thread's loop, or the app's main loop when the thread runs none.
     */
    [[nodiscard]] auto current_loop() -> trantor::EventLoop*;

    /**
     * @brief Read the error text of a OneBot response envelope.
     * @param root Parsed response.
     * @return The `message` field, else the `wording` field, else an empty string.
     */
    [[nodiscard]] auto envelope_message(Json::Value const& root) -> std::string;

    /**
     * @brief Take `data` out of a OneBot response envelope.
     * @param action Action name, for error messages.
     * @param root Parsed response: the body of a 2xx HTTP response, or a WebSocket message.
     * @return `data` when `status` is `ok` or `async`; an `Action` error when it is
     * `failed`; a `ResponseValidation` error when the response is not an envelope.
     */
    [[nodiscard]] auto unwrap_envelope(
        std::string const& action,
        Json::Value const& root
    ) -> Result<Json::Value>;

} // namespace drogon_onebot::details
