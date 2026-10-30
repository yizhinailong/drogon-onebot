/**
 * @file event_receiver.hpp
 * @brief Declare the receiver that checks, parses, and dispatches the events of the OneBot implementation.
 */
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/value.h>

#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Parsed event; `std::monostate` for a `post_type` without handlers.
     */
    using ParsedEvent = std::variant<std::monostate, MessageEvent, NoticeEvent, RequestEvent, MetaEvent>;

    /**
     * @brief Check, parse, and dispatch the events of OneBot v11 HTTP POST reporting and of the reverse WebSocket.
     *
     * The HTTP route answers before any handler runs: 204 for an accepted event, 401 or 403
     * for a missing or wrong `X-Signature`, and 400 for a body that is not an event.
     * Handlers then run through `drogon::async_run` on the IO loop that received the
     * request, so a handler that awaits does not hold the implementation's request open.
     * Events from the reverse WebSocket are neither signed nor answered, and their handlers
     * run on the connection's IO loop.
     */
    class EventReceiver {
    private:
        std::string m_secret;                                ///< Secret that signs events; empty accepts unsigned events.
        std::vector<MessageHandler> m_message_handlers;      ///< Handlers for `post_type` `message`.
        std::vector<NoticeHandler> m_notice_handlers;        ///< Handlers for `post_type` `notice`.
        std::vector<RequestHandler> m_request_handlers;      ///< Handlers for `post_type` `request`.
        std::vector<MetaEventHandler> m_meta_event_handlers; ///< Handlers for `post_type` `meta_event`.
        std::atomic<bool> m_registration_closed{ false };    ///< Set once the listeners have started, after which no handler may be added.

    public:
        /**
         * @brief Create a receiver without handlers.
         * @param secret Secret that signs events; empty accepts unsigned events.
         */
        explicit EventReceiver(std::string secret);

        /**
         * @brief Mark the handler lists as final, because the listeners have started.
         */
        auto CloseRegistration() noexcept -> void;

        /**
         * @brief Tell whether the handler lists are final.
         * @return True once `CloseRegistration()` has run; adding a handler then would race with `Receive()`.
         */
        [[nodiscard]] auto RegistrationClosed() const noexcept -> bool;

        /**
         * @brief Add a handler for message events.
         * @param handler Coroutine to run for each message event.
         */
        auto AddMessageHandler(MessageHandler handler) -> void;

        /**
         * @brief Add a handler for notice events.
         * @param handler Coroutine to run for each notice event.
         */
        auto AddNoticeHandler(NoticeHandler handler) -> void;

        /**
         * @brief Add a handler for request events.
         * @param handler Coroutine to run for each request event.
         */
        auto AddRequestHandler(RequestHandler handler) -> void;

        /**
         * @brief Add a handler for meta events.
         * @param handler Coroutine to run for each meta event.
         */
        auto AddMetaEventHandler(MetaEventHandler handler) -> void;

        /**
         * @brief Answer one posted event, then start its handlers.
         * @param req Request from the OneBot implementation.
         * @param callback Drogon's response callback, called exactly once before any handler runs.
         * @note Event types other than the four above, such as NapCat's `message_sent`,
         * are answered with 204 and dropped.
         */
        auto Receive(
            drogon::HttpRequestPtr const& req,
            std::function<void(drogon::HttpResponsePtr const&)>&& callback
        ) const -> void;

        /**
         * @brief Start the handlers of one event that arrived over the reverse WebSocket.
         * @param root Parsed message, a JSON object with a `post_type`.
         * @note An event that fails to parse is logged and dropped, because a WebSocket push
         * has no answer to carry the reason. Event types other than the four above are dropped.
         */
        auto Deliver(Json::Value const& root) const -> void;

    private:
        /**
         * @brief Start every handler of a parsed event's type, each in its own task.
         * @param event Parsed event; `std::monostate` starts nothing.
         */
        auto dispatchEvent(ParsedEvent const& event) const -> void;
    };

} // namespace drogon_onebot::details
