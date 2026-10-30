/**
 * @file reverse_ws.hpp
 * @brief Declare the reverse WebSocket connection that carries actions to the OneBot implementation and events from it.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <drogon/WebSocketConnection.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>
#include <trantor/net/EventLoop.h>

#include "details/event_receiver.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/error.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Keep the reverse WebSocket connection of one OneBot v11 implementation, and call actions over it.
     *
     * The implementation connects to `ws_path` as a Universal client (`ws-reverse.md` in the
     * botuniverse/onebot-11 spec): it pushes events, and answers each `{ action, params, echo }`
     * request with an envelope that carries the same `echo` (`ws.md`). One connection is open at
     * a time; a new one replaces it, so a stale socket never blocks a reconnect.
     *
     * Calls may start on any thread, and each resumes its caller on the loop it started from, as
     * `HttpTransport` does. Messages arrive on the connection's IO loop.
     */
    class ReverseWebSocket final : public Transport, public std::enable_shared_from_this<ReverseWebSocket> {
    private:
        /**
         * @brief Resumes the coroutine that awaits a call, with the call's result.
         */
        using Completion = std::function<void(Result<Json::Value>)>;

        /**
         * @brief A call that awaits its response.
         */
        struct PendingCall {
            std::string action;                                ///< Action name, for error messages.
            drogon::WebSocketConnection const* connection{};   ///< Connection the request went out on; only compared, never dereferenced.
            trantor::EventLoop* loop{};                        ///< Loop that `complete` runs on.
            trantor::TimerId timer{ trantor::InvalidTimerId }; ///< Timeout timer on `loop`; invalid without a timeout.
            Completion complete;                               ///< Resumes the awaiting coroutine.
        };

        std::string m_path;                                     ///< `ws_path`, for messages.
        std::shared_ptr<EventReceiver const> m_receiver;        ///< Handlers of the events that arrive on the connection.
        std::mutex m_mutex;                                     ///< Guards the members below.
        drogon::WebSocketConnectionPtr m_connection;            ///< Open connection that calls go out on; null without one.
        std::uint64_t m_last_echo{ 0 };                         ///< Last `echo` handed out; each call takes the next.
        std::unordered_map<std::string, PendingCall> m_pending; ///< Calls that await their response, by `echo`.

    public:
        /**
         * @brief Create the endpoint without a connection.
         * @param path `ws_path`, for log and error messages.
         * @param receiver Handlers of the events that arrive on the connection.
         */
        ReverseWebSocket(std::string path, std::shared_ptr<EventReceiver const> receiver);

        /**
         * @brief Make a newly opened connection the one that calls go out on.
         * @param connection Connection that passed the handshake checks.
         * @param self_id `X-Self-ID` of the handshake, for the log.
         * @note A connection that is still open is closed, and its pending calls fail with
         * `ErrorKind::Connection`.
         */
        auto Open(drogon::WebSocketConnectionPtr const& connection, std::string_view self_id) -> void;

        /**
         * @brief Forget a closed connection and fail the calls that still await a response on it.
         * @param connection Connection that closed, open or already replaced.
         */
        auto Close(drogon::WebSocketConnectionPtr const& connection) -> void;

        /**
         * @brief Handle one message of a connection: start the handlers of an event, or complete
         * the call whose `echo` a response carries.
         * @param message Text of the message.
         * @note A message that is neither, such as a response to a call that already timed out,
         * is logged and dropped.
         */
        auto Receive(std::string_view message) -> void;

        /**
         * @brief Send an action over the open connection and await the `data` of its response.
         * @param action Action name, e.g. `send_private_msg`.
         * @param params Action parameters, sent as `params`.
         * @param timeout Seconds to wait for the response; 0 waits until the connection closes.
         * @return Task yielding the envelope's `data` when `status` is `ok` or `async`, or why the
         * action failed: `ErrorKind::Connection` when no connection is open or it closes first.
         * @note The caller keeps this object alive until the task completes.
         */
        [[nodiscard]] auto Call(
            std::string action,
            Json::Value params,
            double timeout
        ) -> drogon::Task<Result<Json::Value>> override;

    private:
        /**
         * @brief Send a request and arrange for `complete` to run once with its result.
         * @param action Action name.
         * @param params Action parameters.
         * @param timeout Seconds to wait for the response; 0 waits until the connection closes.
         * @param complete Runs on the calling thread's loop, never before this returns.
         */
        auto startCall(
            std::string action,
            Json::Value params,
            double timeout,
            Completion complete
        ) -> void;

        /**
         * @brief Remove a pending call by its `echo`.
         * @param echo `echo` of the request.
         * @return The call, or empty when it already ended.
         */
        [[nodiscard]] auto takeCall(std::string const& echo) -> std::optional<PendingCall>;

        /**
         * @brief Remove the pending calls whose request went out on a connection.
         * @param connection Connection that closed or was replaced.
         * @return The removed calls.
         * @note The caller holds `m_mutex`.
         */
        [[nodiscard]] auto takeCallsOn(drogon::WebSocketConnection const* connection) -> std::vector<PendingCall>;

        /**
         * @brief Stop a call's timer and resume its caller with the result.
         * @param call Call removed from `m_pending`.
         * @param result Result to resume with.
         */
        static auto completeCall(PendingCall call, Result<Json::Value> result) -> void;
    };

} // namespace drogon_onebot::details
