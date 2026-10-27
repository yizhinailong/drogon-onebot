/**
 * @file event_receiver.hpp
 * @brief Declare the receiver behind the route that the OneBot implementation posts events to.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Check, parse, and dispatch the events of OneBot v11 HTTP POST reporting.
     *
     * The route answers before any handler runs: 204 for an accepted event, 401 or 403
     * for a missing or wrong `X-Signature`, and 400 for a body that is not an event.
     * Handlers then run through `drogon::async_run` on the IO loop that received the
     * request, so a handler that awaits does not hold the implementation's request open.
     */
    class EventReceiver {
    private:
        std::string m_secret;                                ///< Secret that signs events; empty accepts unsigned events.
        std::vector<MessageHandler> m_message_handlers;      ///< Handlers for `post_type` `message`.
        std::vector<NoticeHandler> m_notice_handlers;        ///< Handlers for `post_type` `notice`.
        std::vector<RequestHandler> m_request_handlers;      ///< Handlers for `post_type` `request`.
        std::vector<MetaEventHandler> m_meta_event_handlers; ///< Handlers for `post_type` `meta_event`.

    public:
        /**
         * @brief Create a receiver without handlers.
         * @param secret Secret that signs events; empty accepts unsigned events.
         */
        explicit EventReceiver(std::string secret);

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
    };

} // namespace drogon_onebot::details
