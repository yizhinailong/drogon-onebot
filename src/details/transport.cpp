/**
 * @file transport.cpp
 * @brief Implement the envelope helpers that the transports share.
 */
#include "details/transport.hpp"

#include <expected>
#include <format>

#include <drogon/HttpAppFramework.h>

namespace drogon_onebot::details {

    /**
     * @brief Pick the event loop that a call runs on and resumes its caller on.
     * @return The calling thread's loop, or the app's main loop when the thread runs none.
     */
    auto current_loop() -> trantor::EventLoop* {
        auto* const loop{ trantor::EventLoop::getEventLoopOfCurrentThread() };
        return loop != nullptr ? loop : drogon::app().getLoop();
    }

    /**
     * @brief Read the error text of a OneBot response envelope.
     * @param root Parsed response.
     * @return The `message` field, else the `wording` field, else an empty string.
     */
    auto envelope_message(Json::Value const& root) -> std::string {
        if (root["message"].isString() && !root["message"].asString().empty()) {
            return root["message"].asString();
        }
        if (root["wording"].isString()) {
            return root["wording"].asString();
        }
        return {};
    }

    /**
     * @brief Take `data` out of a OneBot response envelope.
     * @param action Action name, for error messages.
     * @param root Parsed response: the body of a 2xx HTTP response, or a WebSocket message.
     * @return `data` when `status` is `ok` or `async`; an `Action` error when it is
     * `failed`; a `ResponseValidation` error when the response is not an envelope.
     */
    auto unwrap_envelope(
        std::string const& action,
        Json::Value const& root
    ) -> Result<Json::Value> {
        if (!root.isObject() || !root["status"].isString()) {
            return std::unexpected{ Error{
                .kind = ErrorKind::ResponseValidation,
                .message = std::format("{}: response has no status string", action),
                .body = root.toStyledString(),
            } };
        }
        auto const status{ root["status"].asString() };
        if (status == "ok" || status == "async") {
            return root["data"];
        }
        if (status == "failed") {
            auto const retcode{ root["retcode"].isInt() ? root["retcode"].asInt() : 0 };
            return std::unexpected{ Error{
                .kind = ErrorKind::Action,
                .message = std::format("{}: retcode {}: {}", action, retcode, envelope_message(root)),
                .retcode = retcode,
                .body = root.toStyledString(),
            } };
        }
        return std::unexpected{ Error{
            .kind = ErrorKind::ResponseValidation,
            .message = std::format("{}: unknown status {}", action, status),
            .body = root.toStyledString(),
        } };
    }

} // namespace drogon_onebot::details
