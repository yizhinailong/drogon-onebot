/**
 * @file reverse_ws.cpp
 * @brief Implement the reverse WebSocket connection that carries actions to the OneBot implementation and events from it.
 */
#include "details/reverse_ws.hpp"

#include <coroutine>
#include <expected>
#include <format>
#include <utility>

#include <json/reader.h>
#include <trantor/utils/Logger.h>

#include "details/json.hpp"

namespace {

    using drogon_onebot::Result;

    /**
     * @brief Awaiter that starts a call through a completion callback and resumes with its result.
     * @tparam Start Callable that takes the completion and starts the call. The completion
     * must run once, after `await_suspend()` has returned.
     */
    template <typename Start>
    class CompletionAwaiter {
    private:
        Start m_start;                               ///< Starts the call.
        std::optional<Result<Json::Value>> m_result; ///< Result that the completion stored.

    public:
        /**
         * @brief Store the callable that starts the call.
         * @param start Callable to run in `await_suspend()`.
         */
        explicit CompletionAwaiter(Start start)
            : m_start{ std::move(start) } {}

        /**
         * @brief Always suspend, because the call has not started yet.
         * @return False.
         */
        [[nodiscard]] auto await_ready() const noexcept -> bool {
            return false;
        }

        /**
         * @brief Start the call with a completion that stores the result and resumes `handle`.
         * @param handle The awaiting coroutine.
         */
        auto await_suspend(std::coroutine_handle<> handle) -> void {
            m_start([this, handle](Result<Json::Value> result) {
                m_result = std::move(result);
                handle.resume();
            });
        }

        /**
         * @brief Hand over the call's result.
         * @return The result that the completion stored.
         */
        [[nodiscard]] auto await_resume() -> Result<Json::Value> {
            return std::move(*m_result);
        }
    };

    /**
     * @brief Parse the text of a WebSocket message.
     * @param message Message text.
     * @return The JSON value, or why the text is not JSON.
     */
    auto parse_json(std::string_view message) -> std::expected<Json::Value, std::string> {
        Json::CharReaderBuilder const builder{};
        std::unique_ptr<Json::CharReader> const reader{ builder.newCharReader() };
        Json::Value root{};
        std::string errors{};
        if (!reader->parse(message.data(), message.data() + message.size(), &root, &errors)) {
            return std::unexpected{ std::move(errors) };
        }
        return root;
    }

    /**
     * @brief Build the error of a call that got no response because of the connection.
     * @param message Description, including the action name.
     * @return A `Connection` error.
     */
    auto connection_error(std::string message) -> std::unexpected<drogon_onebot::Error> {
        return std::unexpected{ drogon_onebot::Error{
            .kind = drogon_onebot::ErrorKind::Connection,
            .message = std::move(message),
        } };
    }

} // namespace

namespace drogon_onebot::details {

    /**
     * @brief Create the endpoint without a connection.
     * @param path `ws_path`, for log and error messages.
     * @param receiver Handlers of the events that arrive on the connection.
     */
    ReverseWebSocket::ReverseWebSocket(std::string path, std::shared_ptr<EventReceiver const> receiver)
        : m_path{ std::move(path) },
          m_receiver{ std::move(receiver) } {}

    /**
     * @brief Make a newly opened connection the one that calls go out on.
     * @param connection Connection that passed the handshake checks.
     * @param self_id `X-Self-ID` of the handshake, for the log.
     * @note A connection that is still open is closed, and its pending calls fail with
     * `ErrorKind::Connection`.
     */
    auto ReverseWebSocket::Open(drogon::WebSocketConnectionPtr const& connection, std::string_view self_id) -> void {
        drogon::WebSocketConnectionPtr replaced{};
        std::vector<PendingCall> orphaned{};
        {
            std::lock_guard const lock{ m_mutex };
            replaced = std::exchange(m_connection, connection);
            if (replaced != nullptr) {
                orphaned = takeCallsOn(replaced.get());
            }
        }
        LOG_INFO << "drogon_onebot::OneBotClient: reverse WebSocket on " << m_path << " connected from " << connection->peerAddr().toIpPort() << " with X-Self-ID " << self_id;
        if (replaced == nullptr) {
            return;
        }

        LOG_WARN << "drogon_onebot::OneBotClient: the new reverse WebSocket connection replaces the one from " << replaced->peerAddr().toIpPort() << ", which is closed";
        for (auto& call : orphaned) {
            auto message{ std::format("{}: the reverse WebSocket connection on {} was replaced before the response arrived", call.action, m_path) };
            completeCall(std::move(call), connection_error(std::move(message)));
        }
        replaced->shutdown(drogon::CloseCode::kNormalClosure, "replaced by a new connection");
    }

    /**
     * @brief Forget a closed connection and fail the calls that still await a response on it.
     * @param connection Connection that closed, open or already replaced.
     */
    auto ReverseWebSocket::Close(drogon::WebSocketConnectionPtr const& connection) -> void {
        bool was_open{ false };
        std::vector<PendingCall> orphaned{};
        {
            std::lock_guard const lock{ m_mutex };
            if (m_connection == connection) {
                m_connection.reset();
                was_open = true;
            }
            orphaned = takeCallsOn(connection.get());
        }
        if (was_open) {
            LOG_INFO << "drogon_onebot::OneBotClient: reverse WebSocket on " << m_path << " from " << connection->peerAddr().toIpPort() << " closed";
        }
        for (auto& call : orphaned) {
            // drogon closes a connection when a frame exceeds client_max_websocket_message_size
            // (WebSocketConnectionImpl.cc:374-380), which a large response can do.
            auto message{ std::format("{}: the reverse WebSocket connection on {} closed before the response arrived; drogon closes it when a message exceeds client_max_websocket_message_size", call.action, m_path) };
            completeCall(std::move(call), connection_error(std::move(message)));
        }
    }

    /**
     * @brief Handle one message of a connection: start the handlers of an event, or complete
     * the call whose `echo` a response carries.
     * @param message Text of the message.
     * @note A message that is neither, such as a response to a call that already timed out,
     * is logged and dropped.
     */
    auto ReverseWebSocket::Receive(std::string_view message) -> void {
        auto const root{ parse_json(message) };
        if (!root) {
            LOG_WARN << "drogon_onebot::OneBotClient: dropped a reverse WebSocket message that is not JSON: " << root.error();
            return;
        }
        if (!root->isObject()) {
            LOG_WARN << "drogon_onebot::OneBotClient: dropped a reverse WebSocket message that is not a JSON object";
            return;
        }
        // Events carry a post_type and responses an echo (ws.md); NapCat sends no event with an echo.
        if (root->isMember("post_type")) {
            m_receiver->Deliver(*root);
            return;
        }
        auto const& echo{ member(*root, "echo") };
        if (!echo.isString()) {
            LOG_WARN << "drogon_onebot::OneBotClient: dropped a reverse WebSocket message with neither a post_type nor a string echo";
            return;
        }
        auto call{ takeCall(echo.asString()) };
        if (!call) {
            LOG_DEBUG << "drogon_onebot::OneBotClient: dropped the response with echo " << echo.asString() << ", whose call already ended";
            return;
        }
        auto result{ unwrap_envelope(call->action, *root) };
        completeCall(std::move(*call), std::move(result));
    }

    /**
     * @brief Send an action over the open connection and await the `data` of its response.
     * @param action Action name, e.g. `send_private_msg`.
     * @param params Action parameters, sent as `params`.
     * @param timeout Seconds to wait for the response; 0 waits until the connection closes.
     * @return Task yielding the envelope's `data` when `status` is `ok` or `async`, or why the
     * action failed: `ErrorKind::Connection` when no connection is open or it closes first.
     * @note The caller keeps this object alive until the task completes.
     */
    auto ReverseWebSocket::Call(
        std::string action,
        Json::Value params,
        double timeout
    ) -> drogon::Task<Result<Json::Value>> {
        co_return co_await CompletionAwaiter{ [&](Completion complete) {
            startCall(std::move(action), std::move(params), timeout, std::move(complete));
        } };
    }

    /**
     * @brief Send a request and arrange for `complete` to run once with its result.
     * @param action Action name.
     * @param params Action parameters.
     * @param timeout Seconds to wait for the response; 0 waits until the connection closes.
     * @param complete Runs on the calling thread's loop, never before this returns.
     */
    auto ReverseWebSocket::startCall(
        std::string action,
        Json::Value params,
        double timeout,
        Completion complete
    ) -> void {
        auto* const loop{ current_loop() };
        std::unique_lock lock{ m_mutex };
        auto const connection{ m_connection };
        if (connection == nullptr) {
            lock.unlock();
            auto message{ std::format("{}: no reverse WebSocket connection is open on {}", action, m_path) };
            completeCall(PendingCall{ .action = std::move(action), .loop = loop, .complete = std::move(complete) }, connection_error(std::move(message)));
            return;
        }
        auto const echo{ std::to_string(++m_last_echo) };
        m_pending.emplace(echo, PendingCall{ .action = action, .connection = connection.get(), .loop = loop, .complete = std::move(complete) });
        lock.unlock();

        if (timeout > 0) {
            auto const timer{ loop->runAfter(timeout, [weak = weak_from_this(), echo, timeout] {
                auto const self{ weak.lock() };
                if (self == nullptr) {
                    return;
                }
                if (auto call{ self->takeCall(echo) }) {
                    auto message{ std::format("{}: no response within {} s", call->action, timeout) };
                    completeCall(std::move(*call), std::unexpected{ Error{ .kind = ErrorKind::Timeout, .message = std::move(message) } });
                }
            }) };
            // The call may already have ended; its timer then fires and finds nothing.
            std::lock_guard const timer_lock{ m_mutex };
            if (auto const pending{ m_pending.find(echo) }; pending != m_pending.end()) {
                pending->second.timer = timer;
            }
        }

        Json::Value request{ Json::objectValue };
        request["action"] = std::move(action);
        request["params"] = std::move(params);
        request["echo"] = echo;
        // A request that goes out on a connection that has just closed is dropped by trantor,
        // and Close() fails the call.
        connection->sendJson(request);
    }

    /**
     * @brief Remove a pending call by its `echo`.
     * @param echo `echo` of the request.
     * @return The call, or empty when it already ended.
     */
    auto ReverseWebSocket::takeCall(std::string const& echo) -> std::optional<PendingCall> {
        std::lock_guard const lock{ m_mutex };
        auto node{ m_pending.extract(echo) };
        if (node.empty()) {
            return std::nullopt;
        }
        return std::move(node.mapped());
    }

    /**
     * @brief Remove the pending calls whose request went out on a connection.
     * @param connection Connection that closed or was replaced.
     * @return The removed calls.
     * @note The caller holds `m_mutex`.
     */
    auto ReverseWebSocket::takeCallsOn(drogon::WebSocketConnection const* connection) -> std::vector<PendingCall> {
        std::vector<PendingCall> calls{};
        for (auto pending{ m_pending.begin() }; pending != m_pending.end();) {
            if (pending->second.connection == connection) {
                calls.push_back(std::move(pending->second));
                pending = m_pending.erase(pending);
            } else {
                ++pending;
            }
        }
        return calls;
    }

    /**
     * @brief Stop a call's timer and resume its caller with the result.
     * @param call Call removed from `m_pending`.
     * @param result Result to resume with.
     */
    auto ReverseWebSocket::completeCall(PendingCall call, Result<Json::Value> result) -> void {
        if (call.timer != trantor::InvalidTimerId) {
            call.loop->invalidateTimer(call.timer);
        }
        // Queued, never run in place: startCall() may still run inside await_suspend().
        call.loop->queueInLoop([complete = std::move(call.complete), result = std::move(result)]() mutable {
            complete(std::move(result));
        });
    }

} // namespace drogon_onebot::details
