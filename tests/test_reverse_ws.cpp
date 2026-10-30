/**
 * @file test_reverse_ws.cpp
 * @brief Connect a fake NapCat to the reverse WebSocket route: the handshake checks, its events, and actions sent over the connection.
 *
 * Loads config.json without its `base_url`, so actions go over the connection, and with an
 * `access_token_env`. The fake connects and answers the way NapCat's `websocket-client.ts`
 * does. Tests run in no fixed order, so each opens its own connection. Runs offline.
 *
 * Each rejected handshake logs drogon's `More responses than expected!`: the upgrade's
 * `WebSocketConnectionImpl`, created before routing (`HttpServer.cc:292`), sends a close frame
 * from its destructor after the rejection, which `HttpClient` reads as a second response.
 */
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <drogon/WebSocketClient.h>
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <json/json.h>
#include <trantor/net/EventLoop.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {

    constexpr char const* TOKEN_ENV{ "DROGON_ONEBOT_TEST_TOKEN" };    ///< `access_token_env` that `main()` adds to config.json.
    constexpr char const* ACCESS_TOKEN{ "drogon-onebot-test-token" }; ///< Value that `main()` gives `TOKEN_ENV`.
    constexpr char const* WS_URL{ "ws://127.0.0.1:18080" };           ///< Listener in config.json, as a WebSocket URL.
    constexpr char const* REPLY_MARKER{ "ping" };                     ///< `raw_message` that makes the message handler reply.
    constexpr std::int64_t GROUP_ID{ 30003 };                         ///< Group of the message events that the fake sends.
    constexpr std::size_t LARGE_SIZE{ 256 * 1024 };                   ///< Bytes of a message above drogon's default limit of 128 KiB.
    constexpr auto CLOSE_WAIT{ std::chrono::milliseconds{ 3000 } };   ///< Longest wait for the route to notice a closed connection.

    std::atomic<std::int64_t> g_next_self_id{ 10001 };                ///< `X-Self-ID` of the next fake, so that each connection has its own.

    test_support::Inbox<drogon_onebot::MetaEvent> g_lifecycles;       ///< Lifecycle events, which each fake sends once connected.
    test_support::Inbox<drogon_onebot::MetaEvent> g_meta_events;      ///< Other meta events.
    test_support::Inbox<drogon_onebot::MessageEvent> g_messages;      ///< Message events other than `REPLY_MARKER`.
    test_support::Inbox<drogon_onebot::NoticeEvent> g_notices;        ///< Every notice event.

    /**
     * @brief What the message handler got back when it replied to `REPLY_MARKER`.
     */
    struct Reply {
        drogon_onebot::Result<drogon_onebot::SentMessage> sent; ///< Result of its `SendMsg()`.
        bool same_loop{};                                       ///< Whether it resumed on the loop that it started on.
    };

    test_support::Inbox<Reply> g_replies; ///< Every reply of the message handler.

    /**
     * @brief Builds the fake's answer to a request; empty answers nothing.
     */
    using Responder = std::function<std::optional<Json::Value>(Json::Value const& request)>;

    /**
     * @brief Return the plugin under test.
     * @return The plugin; the app is running.
     */
    auto plugin() -> drogon_onebot::OneBotClient* {
        return drogon::app().getPlugin<drogon_onebot::OneBotClient>();
    }

    /**
     * @brief Parse the text of a WebSocket message.
     * @param text Message text.
     * @return The JSON value, or null when the text is not JSON.
     */
    auto parse(std::string const& text) -> Json::Value {
        Json::CharReaderBuilder const builder{};
        std::unique_ptr<Json::CharReader> const reader{ builder.newCharReader() };
        Json::Value root{};
        std::string errors{};
        if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors)) {
            return Json::Value{};
        }
        return root;
    }

    /**
     * @brief Build NapCat's answer to a request that succeeded (`OB11Response.ok`).
     * @param request Request to answer; its `echo` is returned.
     * @param data The answer's `data`.
     * @return Response envelope.
     */
    auto ok_response(Json::Value const& request, Json::Value data) -> Json::Value {
        Json::Value response{ Json::objectValue };
        response["status"] = "ok";
        response["retcode"] = 0;
        response["data"] = std::move(data);
        response["message"] = "";
        response["wording"] = "";
        response["echo"] = request["echo"];
        response["stream"] = "normal-action";
        return response;
    }

    /**
     * @brief Build NapCat's answer to a request that failed (`OB11Response.error`).
     * @param request Request to answer; its `echo` is returned.
     * @param retcode WebSocket retcode, e.g. 1404 for an unknown action.
     * @param message Error text.
     * @return Response envelope.
     */
    auto failed_response(Json::Value const& request, int retcode, std::string const& message) -> Json::Value {
        Json::Value response{ Json::objectValue };
        response["status"] = "failed";
        response["retcode"] = retcode;
        response["data"] = Json::Value{};
        response["message"] = message;
        response["wording"] = message;
        response["echo"] = request["echo"];
        response["stream"] = "normal-action";
        return response;
    }

    /**
     * @brief Build a responder that answers every request with the same `data`.
     * @param data The answers' `data`.
     * @return Responder.
     */
    auto answer_with(Json::Value data) -> Responder {
        return [data = std::move(data)](Json::Value const& request) -> std::optional<Json::Value> {
            return ok_response(request, data);
        };
    }

    /**
     * @brief Build a meta event the way NapCat does.
     * @param self_id QQ number of the bot account.
     * @param meta_event_type `lifecycle` or `heartbeat`.
     * @return Event without type-specific fields.
     */
    auto meta_event(std::int64_t self_id, char const* meta_event_type) -> Json::Value {
        Json::Value event{ Json::objectValue };
        event["time"] = 1761800000;
        event["self_id"] = self_id;
        event["post_type"] = "meta_event";
        event["meta_event_type"] = meta_event_type;
        return event;
    }

    /**
     * @brief Build a group message event the way NapCat does.
     * @param self_id QQ number of the bot account.
     * @param text Text of the single segment and of `raw_message`.
     * @return Event in array format.
     */
    auto group_message(std::int64_t self_id, std::string const& text) -> Json::Value {
        Json::Value event{ Json::objectValue };
        event["time"] = 1761800000;
        event["self_id"] = self_id;
        event["post_type"] = "message";
        event["message_type"] = "group";
        event["sub_type"] = "normal";
        event["message_id"] = 7;
        event["group_id"] = GROUP_ID;
        event["user_id"] = 20002;
        event["sender"]["user_id"] = 20002;
        event["sender"]["nickname"] = "member";
        event["sender"]["card"] = "";
        event["sender"]["role"] = "member";
        Json::Value segment{ Json::objectValue };
        segment["type"] = "text";
        segment["data"]["text"] = text;
        event["message"].append(segment);
        event["raw_message"] = text;
        event["font"] = 14;
        event["message_format"] = "array";
        return event;
    }

    /**
     * @brief State that the fake's handlers share with it, because they may run after it is gone.
     */
    struct FakeState {
        std::mutex mutex;                              ///< Guards `responder`.
        Responder responder;                           ///< Answers requests; empty answers nothing.
        test_support::Inbox<Json::Value> requests;     ///< Every request received, oldest first.
        test_support::Inbox<std::string> close_frames; ///< Payload of each close frame from the route: the code, then the reason.
    };

    /**
     * @brief Reverse WebSocket client that plays NapCat.
     *
     * Connects with the headers of `websocket-client.ts`, sends the lifecycle `connect` event,
     * and waits until a handler has received it, so the route has made this connection the
     * one that calls go out on. drogon's client reconnects a second after any disconnect that
     * `stop()` did not start, so the fake stops itself on the route's close frame.
     */
    class FakeNapCat {
    private:
        std::shared_ptr<FakeState> m_state{ std::make_shared<FakeState>() };                        ///< Shared with the handlers.
        drogon::WebSocketClientPtr m_client{ drogon::WebSocketClient::newWebSocketClient(WS_URL) }; ///< Client on the main loop.
        std::int64_t m_self_id{ g_next_self_id++ };                                                 ///< `X-Self-ID` of this connection.
        bool m_ready{ false };                                                                      ///< Whether the route accepted the connection.

    public:
        /**
         * @brief Connect to the route and wait until it has taken the connection.
         * @param authorization `Authorization` header; NapCat sends `Bearer <token>`.
         */
        explicit FakeNapCat(std::string const& authorization = std::string{ "Bearer " } + ACCESS_TOKEN) {
            m_client->setMessageHandler([state = m_state](std::string&& message, drogon::WebSocketClientPtr const& client, drogon::WebSocketMessageType const& type) {
                if (type == drogon::WebSocketMessageType::Close) {
                    state->close_frames.Push(std::move(message));
                    client->getLoop()->queueInLoop([client] { client->stop(); });
                    return;
                }
                if (type != drogon::WebSocketMessageType::Text) {
                    return;
                }
                auto request{ parse(message) };
                Responder responder{};
                {
                    std::lock_guard const lock{ state->mutex };
                    responder = state->responder;
                }
                state->requests.Push(request);
                if (!responder) {
                    return;
                }
                if (auto const answer{ responder(request) }) {
                    client->getConnection()->sendJson(*answer);
                }
            });

            auto request{ drogon::HttpRequest::newHttpRequest() };
            request->setPath(test_support::WS_PATH);
            request->addHeader("X-Self-ID", std::to_string(m_self_id));
            request->addHeader("Authorization", authorization);
            request->addHeader("x-client-role", "Universal");
            request->addHeader("User-Agent", "OneBot/11");
            auto const handshake{ std::make_shared<std::promise<drogon::ReqResult>>() };
            auto const answered{ std::make_shared<std::once_flag>() };
            m_client->connectToServer(request, [handshake, answered](drogon::ReqResult result, drogon::HttpResponsePtr const&, drogon::WebSocketClientPtr const&) {
                std::call_once(*answered, [&] { handshake->set_value(result); });
            });
            auto result{ handshake->get_future() };
            if (result.wait_for(test_support::EVENT_WAIT) != std::future_status::ready || result.get() != drogon::ReqResult::Ok) {
                return;
            }

            auto lifecycle{ meta_event(m_self_id, "lifecycle") };
            lifecycle["sub_type"] = "connect";
            Push(lifecycle);
            while (auto const event{ g_lifecycles.Pop(test_support::EVENT_WAIT) }) {
                if (event->self_id == m_self_id) {
                    m_ready = event->sub_type == "connect";
                    break;
                }
            }
        }

        /**
         * @brief Close the connection.
         */
        ~FakeNapCat() {
            m_client->getLoop()->queueInLoop([client = m_client] { client->stop(); });
        }

        FakeNapCat(FakeNapCat const&) = delete;
        auto operator=(FakeNapCat const&) -> FakeNapCat& = delete;

        /**
         * @brief Tell whether the route took the connection.
         * @return True once a handler has received this fake's lifecycle event.
         */
        [[nodiscard]] auto Ready() const -> bool {
            return m_ready;
        }

        /**
         * @brief Return the `X-Self-ID` of the connection.
         * @return QQ number that the fake plays.
         */
        [[nodiscard]] auto SelfId() const -> std::int64_t {
            return m_self_id;
        }

        /**
         * @brief Answer the requests that arrive from now on with `responder`.
         * @param responder Builds each answer; empty answers nothing.
         */
        auto Answer(Responder responder) -> void {
            std::lock_guard const lock{ m_state->mutex };
            m_state->responder = std::move(responder);
        }

        /**
         * @brief Send a message to the route, e.g. an event or a late answer.
         * @param message JSON message.
         */
        auto Push(Json::Value const& message) -> void {
            m_client->getConnection()->sendJson(message);
        }

        /**
         * @brief Send raw text to the route.
         * @param text Message text, e.g. something that is not JSON.
         */
        auto PushText(std::string const& text) -> void {
            m_client->getConnection()->send(text);
        }

        /**
         * @brief Take the oldest request that the route sent.
         * @return The request, or empty if none arrived in time.
         */
        [[nodiscard]] auto NextRequest() -> std::optional<Json::Value> {
            return m_state->requests.Pop(test_support::EVENT_WAIT);
        }

        /**
         * @brief Take the payload of the route's close frame.
         * @return The close code and reason, or empty if the route did not close the connection in time.
         */
        [[nodiscard]] auto NextCloseFrame() -> std::optional<std::string> {
            return m_state->close_frames.Pop(test_support::EVENT_WAIT);
        }
    };

    /**
     * @brief Call `get_status` on another thread, so that the test thread can play NapCat meanwhile.
     * @param timeout Seconds to wait for the response; 0 waits until the connection closes.
     * @return Future of the call's result.
     */
    auto call_in_background(double timeout = 30.0) -> std::future<drogon_onebot::Result<Json::Value>> {
        return std::async(std::launch::async, [bot = plugin()->NewOneBotClient(), timeout] {
            return drogon::sync_wait(bot.CallAction({ .action = "get_status", .timeout = timeout }));
        });
    }

    /**
     * @brief Call actions until the route reports that no connection is open, e.g. after a fake closed.
     * @return The last result.
     */
    auto call_until_disconnected() -> drogon_onebot::Result<Json::Value> {
        auto const bot{ plugin()->NewOneBotClient() };
        auto const deadline{ std::chrono::steady_clock::now() + CLOSE_WAIT };
        while (true) {
            auto result{ drogon::sync_wait(bot.CallAction({ .action = "get_status", .timeout = 0.5 })) };
            if ((!result && result.error().message.contains("no reverse WebSocket connection")) || std::chrono::steady_clock::now() >= deadline) {
                return result;
            }
        }
    }

    /**
     * @brief Send an upgrade request to the route without a WebSocket client and return the status.
     *
     * drogon's WebSocket client reports a refused handshake without the response, so this sends
     * the handshake through `HttpClient` with `setPassThrough`, which keeps its `Connection` header.
     * @param authorization `Authorization` header; empty sends none.
     * @param role `X-Client-Role` header.
     * @return The status of the route's answer.
     * @throws std::runtime_error If no answer arrives.
     */
    auto handshake_status(std::string const& authorization, std::string const& role) -> drogon::HttpStatusCode {
        auto request{ drogon::HttpRequest::newHttpRequest() };
        request->setPassThrough(true);
        request->setMethod(drogon::Get);
        request->setPath(test_support::WS_PATH);
        request->addHeader("Connection", "Upgrade");
        request->addHeader("Upgrade", "websocket");
        request->addHeader("Sec-WebSocket-Version", "13");
        request->addHeader("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
        request->addHeader("X-Self-ID", "10000");
        request->addHeader("X-Client-Role", role);
        if (!authorization.empty()) {
            request->addHeader("Authorization", authorization);
        }
        auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ std::format("GET {}: {}", test_support::WS_PATH, drogon::to_string_view(result)) };
        }
        return response->getStatusCode();
    }

    /**
     * @brief Register the handlers that fill the inboxes.
     *
     * Runs as a beginning advice, before the listeners start.
     */
    auto register_handlers() -> void {
        plugin()->OnMetaEvent([](drogon_onebot::MetaEvent event) -> drogon::Task<> {
            (event.meta_event_type == "lifecycle" ? g_lifecycles : g_meta_events).Push(std::move(event));
            co_return;
        });
        plugin()->OnNotice([](drogon_onebot::NoticeEvent event) -> drogon::Task<> {
            g_notices.Push(std::move(event));
            co_return;
        });
        plugin()->OnMessage([](drogon_onebot::MessageEvent event) -> drogon::Task<> {
            if (event.raw_message != REPLY_MARKER) {
                g_messages.Push(std::move(event));
                co_return;
            }
            auto const* const loop{ trantor::EventLoop::getEventLoopOfCurrentThread() };
            auto const bot{ plugin()->NewOneBotClient() };
            auto sent{ co_await bot.SendMsg({
                .message_type = event.message_type,
                .user_id = event.user_id,
                .group_id = event.group_id,
                .message = { drogon_onebot::MessageSegment::Text("pong") },
            }) };
            g_replies.Push(Reply{
                .sent = std::move(sent),
                .same_loop = trantor::EventLoop::getEventLoopOfCurrentThread() == loop,
            });
        });
    }

} // namespace

DROGON_TEST(ReadsWsOnlyConfig) {
    auto const& config{ plugin()->Config() };
    CHECK(config.base_url.empty());
    CHECK(config.ws_path == test_support::WS_PATH);
    CHECK(config.access_token == ACCESS_TOKEN);
}

DROGON_TEST(RejectsBadHandshakes) {
    auto const bearer{ std::string{ "Bearer " } + ACCESS_TOKEN };
    CHECK(handshake_status("", "Universal") == drogon::k401Unauthorized);
    CHECK(handshake_status("Bearer wrong-token", "Universal") == drogon::k403Forbidden);
    CHECK(handshake_status("Bearer ", "Universal") == drogon::k403Forbidden);
    CHECK(handshake_status(ACCESS_TOKEN, "Universal") == drogon::k403Forbidden);
    CHECK(handshake_status(bearer, "Event") == drogon::k400BadRequest);
    CHECK(handshake_status(bearer, "API") == drogon::k400BadRequest);

    FakeNapCat const wrong_token{ "Bearer wrong-token" };
    CHECK(!wrong_token.Ready());
}

DROGON_TEST(DeliversEventsFromTheConnection) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());

    napcat.Push(group_message(napcat.SelfId(), "hello over ws"));
    auto const message{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(message.has_value());
    CHECK(message->self_id == napcat.SelfId());
    CHECK(message->group_id == GROUP_ID);
    CHECK(message->raw_message == "hello over ws");
    CHECK(message->sender.nickname == "member");

    auto heartbeat{ meta_event(napcat.SelfId(), "heartbeat") };
    heartbeat["status"]["online"] = true;
    heartbeat["status"]["good"] = true;
    heartbeat["interval"] = 30000;
    napcat.Push(heartbeat);
    auto const meta{ g_meta_events.Pop(test_support::EVENT_WAIT) };
    REQUIRE(meta.has_value());
    CHECK(meta->meta_event_type == "heartbeat");
    CHECK(meta->interval == 30000);
    REQUIRE(meta->status.has_value());
    CHECK(meta->status->online == true);

    Json::Value recall{ Json::objectValue };
    recall["time"] = 1761800000;
    recall["self_id"] = napcat.SelfId();
    recall["post_type"] = "notice";
    recall["notice_type"] = "group_recall";
    recall["group_id"] = GROUP_ID;
    recall["user_id"] = 20002;
    recall["operator_id"] = 20002;
    recall["message_id"] = 7;
    napcat.Push(recall);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    auto const* const recalled{ std::get_if<drogon_onebot::GroupRecallNotice>(&notice->detail) };
    REQUIRE(recalled != nullptr);
    CHECK(recalled->message_id == 7);
}

DROGON_TEST(DropsBadMessagesAndKeepsTheConnection) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());

    napcat.PushText("not json");
    napcat.PushText("[1, 2]");
    auto no_user{ group_message(napcat.SelfId(), "no user_id") };
    no_user.removeMember("user_id");
    napcat.Push(no_user);
    Json::Value no_echo{ Json::objectValue };
    no_echo["status"] = "ok";
    napcat.Push(no_echo);

    napcat.Push(group_message(napcat.SelfId(), "after the bad ones"));
    auto const message{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(message.has_value());
    CHECK(message->raw_message == "after the bad ones");
}

DROGON_TEST(CallsActionsOverTheConnection) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());
    Json::Value login{ Json::objectValue };
    login["user_id"] = napcat.SelfId();
    login["nickname"] = "NapCat";
    napcat.Answer(answer_with(login));

    auto const info{ drogon::sync_wait(plugin()->NewOneBotClient().GetLoginInfo()) };
    REQUIRE(info.has_value());
    CHECK(info->user_id == napcat.SelfId());
    CHECK(info->nickname == "NapCat");

    auto const request{ napcat.NextRequest() };
    REQUIRE(request.has_value());
    CHECK((*request)["action"].asString() == "get_login_info");
    CHECK((*request)["params"].isObject());
    CHECK((*request)["echo"].isString());
}

DROGON_TEST(MatchesResponsesByEcho) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());
    auto const bot{ plugin()->NewOneBotClient() };
    auto call{ [bot](int n) {
        return std::async(std::launch::async, [bot, n] {
            Json::Value params{ Json::objectValue };
            params["n"] = n;
            return drogon::sync_wait(bot.CallAction({ .action = "echo_n", .params = params }));
        });
    } };
    auto first{ call(1) };
    auto const first_request{ napcat.NextRequest() };
    auto second{ call(2) };
    auto const second_request{ napcat.NextRequest() };
    REQUIRE(first_request.has_value());
    REQUIRE(second_request.has_value());
    CHECK((*first_request)["echo"] != (*second_request)["echo"]);

    napcat.Push(ok_response(*second_request, (*second_request)["params"]["n"]));
    napcat.Push(ok_response(*first_request, (*first_request)["params"]["n"]));
    auto const first_result{ first.get() };
    auto const second_result{ second.get() };
    REQUIRE(first_result.has_value());
    REQUIRE(second_result.has_value());
    CHECK(first_result->asInt() == 1);
    CHECK(second_result->asInt() == 2);
}

DROGON_TEST(ReportsFailedAndMalformedResponses) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());
    auto const bot{ plugin()->NewOneBotClient() };

    napcat.Answer([](Json::Value const& request) -> std::optional<Json::Value> {
        return failed_response(request, 1404, "unsupported API " + request["action"].asString());
    });
    auto const unknown{ drogon::sync_wait(bot.CallAction({ .action = "no_such_action" })) };
    REQUIRE(!unknown.has_value());
    CHECK(unknown.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(unknown.error().retcode == 1404);
    CHECK(unknown.error().message.contains("unsupported API no_such_action"));

    napcat.Answer([](Json::Value const& request) -> std::optional<Json::Value> {
        Json::Value response{ Json::objectValue };
        response["echo"] = request["echo"];
        response["data"] = 1;
        return response;
    });
    auto const malformed{ drogon::sync_wait(bot.CallAction({ .action = "get_status" })) };
    REQUIRE(!malformed.has_value());
    CHECK(malformed.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(malformed.error().message.contains("no status string"));

    napcat.Answer(answer_with(Json::Value{}));
    auto const unreadable{ drogon::sync_wait(bot.GetLoginInfo()) };
    REQUIRE(!unreadable.has_value());
    CHECK(unreadable.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
}

DROGON_TEST(TimesOutWithoutResponse) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());
    auto const bot{ plugin()->NewOneBotClient() };

    auto const late{ drogon::sync_wait(bot.CallAction({ .action = "get_status", .timeout = 0.3 })) };
    REQUIRE(!late.has_value());
    CHECK(late.error().kind == drogon_onebot::ErrorKind::Timeout);
    CHECK(late.error().message.contains("no response within"));

    auto const request{ napcat.NextRequest() };
    REQUIRE(request.has_value());
    napcat.Push(ok_response(*request, Json::Value{}));
    napcat.Answer(answer_with(Json::Value{ Json::objectValue }));
    CHECK(drogon::sync_wait(bot.CallAction({ .action = "get_status" })).has_value());
}

DROGON_TEST(FailsWithoutConnection) {
    auto const result{ call_until_disconnected() };
    REQUIRE(!result.has_value());
    CHECK(result.error().kind == drogon_onebot::ErrorKind::Connection);
    CHECK(result.error().message.contains("no reverse WebSocket connection is open on /onebot/ws"));
}

DROGON_TEST(FailsPendingCallWhenConnectionCloses) {
    auto napcat{ std::make_unique<FakeNapCat>() };
    REQUIRE(napcat->Ready());
    auto pending{ call_in_background(0) };
    REQUIRE(napcat->NextRequest().has_value());

    napcat.reset();
    REQUIRE(pending.wait_for(CLOSE_WAIT) == std::future_status::ready);
    auto const result{ pending.get() };
    REQUIRE(!result.has_value());
    CHECK(result.error().kind == drogon_onebot::ErrorKind::Connection);
    CHECK(result.error().message.contains("closed before the response arrived"));
}

DROGON_TEST(NewConnectionReplacesOpenOne) {
    FakeNapCat first{};
    REQUIRE(first.Ready());
    auto pending{ call_in_background() };
    REQUIRE(first.NextRequest().has_value());

    FakeNapCat second{};
    REQUIRE(second.Ready());
    auto const close_frame{ first.NextCloseFrame() };
    REQUIRE(close_frame.has_value());
    CHECK(close_frame->contains("replaced by a new connection"));
    REQUIRE(pending.wait_for(CLOSE_WAIT) == std::future_status::ready);
    auto const orphaned{ pending.get() };
    REQUIRE(!orphaned.has_value());
    CHECK(orphaned.error().kind == drogon_onebot::ErrorKind::Connection);
    CHECK(orphaned.error().message.contains("was replaced before the response arrived"));

    second.Answer(answer_with(Json::Value{ Json::objectValue }));
    CHECK(drogon::sync_wait(plugin()->NewOneBotClient().CallAction({ .action = "get_status" })).has_value());
}

DROGON_TEST(CarriesMessagesAboveDrogonsDefaultLimit) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());
    Json::Value data{ Json::objectValue };
    data["text"] = std::string(LARGE_SIZE, 'x');
    napcat.Answer(answer_with(data));

    auto const result{ drogon::sync_wait(plugin()->NewOneBotClient().CallAction({ .action = "get_large" })) };
    REQUIRE(result.has_value());
    CHECK((*result)["text"].asString().size() == LARGE_SIZE);
}

DROGON_TEST(HandlerRepliesOverTheConnection) {
    FakeNapCat napcat{};
    REQUIRE(napcat.Ready());
    Json::Value sent{ Json::objectValue };
    sent["message_id"] = 42;
    napcat.Answer(answer_with(sent));

    napcat.Push(group_message(napcat.SelfId(), REPLY_MARKER));
    auto const reply{ g_replies.Pop(test_support::EVENT_WAIT) };
    REQUIRE(reply.has_value());
    REQUIRE(reply->sent.has_value());
    CHECK(reply->sent->message_id == 42);
    CHECK(reply->same_loop);

    auto const request{ napcat.NextRequest() };
    REQUIRE(request.has_value());
    CHECK((*request)["action"].asString() == "send_msg");
    CHECK((*request)["params"]["message_type"].asString() == "group");
    CHECK((*request)["params"]["group_id"].asInt64() == GROUP_ID);
    CHECK((*request)["params"]["message"][0]["data"]["text"].asString() == "pong");
}

auto main(int argc, char** argv) -> int {
    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    test_support::set_env(TOKEN_ENV, ACCESS_TOKEN);
    auto config{ test_support::read_config() };
    auto& plugin_config{ config["plugins"][0]["config"] };
    plugin_config.removeMember("base_url");
    plugin_config["access_token_env"] = TOKEN_ENV;
    drogon::app().loadConfigJson(std::move(config)).registerBeginningAdvice(register_handlers);
    return test_support::run_tests_with_app(argc, argv);
}
