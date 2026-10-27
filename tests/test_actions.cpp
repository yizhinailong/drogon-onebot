/**
 * @file test_actions.cpp
 * @brief Call actions on a fake OneBot HTTP API that the same app serves, NapCat-style.
 *
 * Covers `CallAction` (request shape, `Authorization`, the `base_url` path prefix, and
 * every `ErrorKind`) and the typed actions. Runs offline.
 */
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/coroutine.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {

    constexpr std::int64_t SELF_ID{ 10001 };         ///< `user_id` of the fake `get_login_info`.
    constexpr std::int64_t PREFIX_SELF_ID{ 10002 };  ///< `user_id` of the fake `get_login_info` under `/prefix`.
    constexpr std::int64_t TARGET_USER{ 20002 };     ///< `user_id` that the fake `send_private_msg` accepts.
    constexpr std::int64_t TARGET_GROUP{ 30003 };    ///< `group_id` that the fake `send_group_msg` accepts.
    constexpr std::int32_t PRIVATE_MESSAGE_ID{ 42 }; ///< `message_id` of the fake `send_private_msg`.
    constexpr std::int32_t GROUP_MESSAGE_ID{ 43 };   ///< `message_id` of the fake `send_group_msg`.
    constexpr int FAILED_RETCODE{ 100 };             ///< `retcode` of the fake `delete_msg`.
    constexpr double SLOW_DELAY{ 1.0 };              ///< Seconds that the fake `slow` action takes.

    /**
     * @brief Build a OneBot v11 response envelope with NapCat's extra `message` and `wording`.
     * @param status `ok`, `async`, or `failed`.
     * @param retcode 0 for `ok`, 1 for `async`, otherwise an error code.
     * @param data Response data; null for `async` and `failed`.
     * @param message Error text for both `message` and `wording`; empty on success.
     * @return Response carrying the envelope with HTTP 200.
     */
    auto envelope(
        std::string const& status,
        int retcode,
        Json::Value const& data,
        std::string const& message
    ) -> drogon::HttpResponsePtr {
        Json::Value root{ Json::objectValue };
        root["status"] = status;
        root["retcode"] = retcode;
        root["data"] = data;
        root["message"] = message;
        root["wording"] = message;
        return drogon::HttpResponse::newHttpJsonResponse(root);
    }

    /**
     * @brief Build an `ok` envelope.
     * @param data Response data.
     * @return Response carrying the envelope with HTTP 200.
     */
    auto ok(Json::Value const& data) -> drogon::HttpResponsePtr {
        return envelope("ok", 0, data, "");
    }

    /**
     * @brief Build the `data` of `get_login_info`.
     * @param user_id Account number, as a JSON number or string.
     * @return Object with `user_id` and `nickname`.
     */
    auto login_info(Json::Value const& user_id) -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["user_id"] = user_id;
        data["nickname"] = "drogon-onebot";
        return data;
    }

    /**
     * @brief Build the `data` of a send action.
     * @param message_id ID of the sent message.
     * @return Object with `message_id`.
     */
    auto sent(std::int32_t message_id) -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["message_id"] = message_id;
        return data;
    }

    /**
     * @brief Build the one-segment text message that the send tests use.
     * @return `[{ "type": "text", "data": { "text": "hi" } }]` as a `Message`.
     */
    auto hi_message() -> drogon_onebot::Message {
        Json::Value text{ Json::objectValue };
        text["text"] = "hi";
        return { { .type = "text", .data = text } };
    }

    /**
     * @brief Check the parameters of a send action as NapCat would receive them.
     * @param req Request to the fake action.
     * @param target_key `user_id` or `group_id`.
     * @param target Value that `target_key` must have.
     * @param message_id ID to answer with.
     * @return `ok` with `message_id`, or `failed` with retcode 1400 and what was wrong.
     */
    auto check_send(
        drogon::HttpRequestPtr const& req,
        char const* target_key,
        std::int64_t target,
        std::int32_t message_id
    ) -> drogon::HttpResponsePtr {
        auto const& json{ req->getJsonObject() };
        if (json == nullptr || (*json)[target_key].asInt64() != target) {
            return envelope("failed", 1400, Json::Value{}, std::string{ "wrong " } + target_key);
        }
        auto const& message{ (*json)["message"] };
        if (!message.isArray() || message.size() != 1 || message[0]["type"].asString() != "text" || message[0]["data"]["text"].asString() != "hi") {
            return envelope("failed", 1400, Json::Value{}, "wrong message: " + message.toStyledString());
        }
        return ok(sent(message_id));
    }

    /**
     * @brief Serve `make(req)` at `POST path` on the app's listener.
     * @param path Request path, e.g. `/get_login_info`.
     * @param make Builds the response for each request.
     */
    auto serve(
        std::string const& path,
        std::function<drogon::HttpResponsePtr(drogon::HttpRequestPtr const&)> make
    ) -> void {
        drogon::app().registerHandler(
            path,
            [make = std::move(make)](
                drogon::HttpRequestPtr const& req,
                std::function<void(drogon::HttpResponsePtr const&)>&& callback
            ) {
                callback(make(req));
            },
            { drogon::Post }
        );
    }

    /**
     * @brief Register the fake NapCat HTTP API on the app's listener.
     */
    auto serve_fake_napcat() -> void {
        serve("/get_login_info", [](auto const&) { return ok(login_info(SELF_ID)); });
        serve("/prefix/get_login_info", [](auto const&) { return ok(login_info(PREFIX_SELF_ID)); });
        serve("/string_id/get_login_info", [](auto const&) { return ok(login_info(std::to_string(SELF_ID))); });
        serve("/echo", [](drogon::HttpRequestPtr const& req) {
            Json::Value data{ Json::objectValue };
            data["body"] = req->getJsonObject() != nullptr ? *req->getJsonObject() : Json::Value{};
            data["authorization"] = req->getHeader("authorization");
            return ok(data);
        });
        serve("/async_action", [](auto const&) { return envelope("async", 1, Json::Value{}, ""); });
        serve("/delete_msg", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "message not found");
        });
        serve("/wording_only", [](auto const&) {
            Json::Value root{ Json::objectValue };
            root["status"] = "failed";
            root["retcode"] = 200;
            root["message"] = "";
            root["wording"] = "unsupported API";
            return drogon::HttpResponse::newHttpJsonResponse(root);
        });
        serve("/forbidden", [](auto const&) {
            Json::Value root{ Json::objectValue };
            root["message"] = "token verify failed!";
            auto response{ drogon::HttpResponse::newHttpJsonResponse(root) };
            response->setStatusCode(drogon::k403Forbidden);
            return response;
        });
        serve("/not_json", [](auto const&) {
            auto response{ drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_TEXT_PLAIN) };
            response->setBody("NapCat4 Is Running");
            return response;
        });
        serve("/no_status", [](auto const&) {
            Json::Value root{ Json::objectValue };
            root["retcode"] = 0;
            return drogon::HttpResponse::newHttpJsonResponse(root);
        });
        serve("/odd_status", [](auto const&) { return envelope("weird", 0, Json::Value{}, ""); });
        serve("/send_private_msg", [](drogon::HttpRequestPtr const& req) {
            return check_send(req, "user_id", TARGET_USER, PRIVATE_MESSAGE_ID);
        });
        serve("/send_group_msg", [](drogon::HttpRequestPtr const& req) {
            return check_send(req, "group_id", TARGET_GROUP, GROUP_MESSAGE_ID);
        });
        serve("/bad/get_login_info", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["nickname"] = "no user_id";
            return ok(data);
        });
        serve("/bad/send_private_msg", [](auto const&) { return ok(Json::Value{ Json::objectValue }); });
        serve("/failing/send_group_msg", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "group muted");
        });
        drogon::app().registerHandler(
            "/slow",
            [](drogon::HttpRequestPtr /*req*/) -> drogon::Task<drogon::HttpResponsePtr> {
                co_await drogon::sleepCoro(trantor::EventLoop::getEventLoopOfCurrentThread(), SLOW_DELAY);
                co_return ok(Json::Value{});
            },
            { drogon::Post }
        );
    }

    /**
     * @brief Return the client that the plugin builds from config.json.
     * @return Client for `SERVER_URL` without an access token.
     */
    auto plugin_bot() -> drogon_onebot::OneBot {
        return drogon::app().getPlugin<drogon_onebot::OneBotClient>()->NewOneBotClient();
    }

    /**
     * @brief Build a client for a path under the fake API, bypassing the plugin.
     * @param path_prefix Path appended to `SERVER_URL`, e.g. `/prefix`.
     * @param access_token Token to send; empty sends no `Authorization` header.
     * @return Client for `SERVER_URL + path_prefix`.
     */
    auto bot_at(
        std::string const& path_prefix,
        std::string access_token = {}
    ) -> drogon_onebot::OneBot {
        return drogon_onebot::OneBot{ drogon_onebot::OneBotConfig{
            .base_url = test_support::SERVER_URL + path_prefix,
            .access_token = std::move(access_token),
        } };
    }

} // namespace

DROGON_TEST(CallActionReturnsData) {
    auto const result{ drogon::sync_wait(plugin_bot().CallAction({ .action = "get_login_info" })) };
    REQUIRE(result.has_value());
    CHECK((*result)["user_id"].asInt64() == SELF_ID);
    CHECK((*result)["nickname"].asString() == "drogon-onebot");
}

DROGON_TEST(CallActionSendsParamsAsJsonBody) {
    Json::Value params{ Json::objectValue };
    params["message_id"] = 7;
    params["note"] = "x";
    auto const result{ drogon::sync_wait(plugin_bot().CallAction({ .action = "echo", .params = params })) };
    REQUIRE(result.has_value());
    CHECK((*result)["body"] == params);
}

DROGON_TEST(CallActionSendsBearerTokenOnlyWhenSet) {
    auto const with_token{ drogon::sync_wait(bot_at("", "t0k3n").CallAction({ .action = "echo" })) };
    REQUIRE(with_token.has_value());
    CHECK((*with_token)["authorization"].asString() == "Bearer t0k3n");

    auto const without_token{ drogon::sync_wait(plugin_bot().CallAction({ .action = "echo" })) };
    REQUIRE(without_token.has_value());
    CHECK((*without_token)["authorization"].asString().empty());
}

DROGON_TEST(CallActionUsesBaseUrlPathAsPrefix) {
    auto const result{ drogon::sync_wait(bot_at("/prefix/").CallAction({ .action = "get_login_info" })) };
    REQUIRE(result.has_value());
    CHECK((*result)["user_id"].asInt64() == PREFIX_SELF_ID);
}

DROGON_TEST(CallActionAsyncYieldsNull) {
    auto const result{ drogon::sync_wait(plugin_bot().CallAction({ .action = "async_action" })) };
    REQUIRE(result.has_value());
    CHECK(result->isNull());
}

DROGON_TEST(CallActionFailedIsActionError) {
    Json::Value params{ Json::objectValue };
    params["message_id"] = 1;
    auto const result{ drogon::sync_wait(plugin_bot().CallAction({ .action = "delete_msg", .params = params })) };
    REQUIRE(!result.has_value());
    CHECK(result.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(result.error().retcode == FAILED_RETCODE);
    CHECK(result.error().message.contains("message not found"));
    CHECK(!result.error().body.empty());
}

DROGON_TEST(CallActionFallsBackToWording) {
    auto const result{ drogon::sync_wait(plugin_bot().CallAction({ .action = "wording_only" })) };
    REQUIRE(!result.has_value());
    CHECK(result.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(result.error().retcode == 200);
    CHECK(result.error().message.contains("unsupported API"));
}

DROGON_TEST(CallActionNon2xxIsStatusError) {
    auto const missing{ drogon::sync_wait(plugin_bot().CallAction({ .action = "no_such_action" })) };
    REQUIRE(!missing.has_value());
    CHECK(missing.error().kind == drogon_onebot::ErrorKind::Status);
    CHECK(missing.error().status_code == 404);

    auto const forbidden{ drogon::sync_wait(plugin_bot().CallAction({ .action = "forbidden" })) };
    REQUIRE(!forbidden.has_value());
    CHECK(forbidden.error().kind == drogon_onebot::ErrorKind::Status);
    CHECK(forbidden.error().status_code == 403);
    CHECK(forbidden.error().message.contains("token verify failed!"));
}

DROGON_TEST(CallActionMalformedEnvelopeIsResponseValidation) {
    for (auto const* action : { "not_json", "no_status", "odd_status" }) {
        auto const result{ drogon::sync_wait(plugin_bot().CallAction({ .action = action })) };
        REQUIRE(!result.has_value());
        CHECK(result.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
        CHECK(!result.error().body.empty());
    }
}

DROGON_TEST(CallActionTimeout) {
    auto const result{ drogon::sync_wait(plugin_bot().CallAction({ .action = "slow", .timeout = 0.2 })) };
    REQUIRE(!result.has_value());
    CHECK(result.error().kind == drogon_onebot::ErrorKind::Timeout);
}

DROGON_TEST(CallActionUnreachableIsConnectionError) {
    drogon_onebot::OneBot const bot{ drogon_onebot::OneBotConfig{ .base_url = "http://127.0.0.1:1" } };
    auto const result{ drogon::sync_wait(bot.CallAction({ .action = "get_login_info", .timeout = 2.0 })) };
    REQUIRE(!result.has_value());
    CHECK(result.error().kind == drogon_onebot::ErrorKind::Connection);
}

DROGON_TEST(GetLoginInfoReadsAccount) {
    auto const info{ drogon::sync_wait(plugin_bot().GetLoginInfo()) };
    REQUIRE(info.has_value());
    CHECK(info->user_id == SELF_ID);
    CHECK(info->nickname == "drogon-onebot");

    auto const string_id{ drogon::sync_wait(bot_at("/string_id").GetLoginInfo()) };
    REQUIRE(string_id.has_value());
    CHECK(string_id->user_id == SELF_ID);
}

DROGON_TEST(GetLoginInfoRejectsMissingUserId) {
    auto const info{ drogon::sync_wait(bot_at("/bad").GetLoginInfo()) };
    REQUIRE(!info.has_value());
    CHECK(info.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(info.error().message.contains("user_id"));
}

DROGON_TEST(SendPrivateMsgSendsSegments) {
    auto const sent_message{
        drogon::sync_wait(plugin_bot().SendPrivateMsg({ .user_id = TARGET_USER, .message = hi_message() }))
    };
    REQUIRE(sent_message.has_value());
    CHECK(sent_message->message_id == PRIVATE_MESSAGE_ID);
}

DROGON_TEST(SendGroupMsgSendsSegments) {
    auto const sent_message{
        drogon::sync_wait(plugin_bot().SendGroupMsg({ .group_id = TARGET_GROUP, .message = hi_message() }))
    };
    REQUIRE(sent_message.has_value());
    CHECK(sent_message->message_id == GROUP_MESSAGE_ID);
}

DROGON_TEST(SendMsgRejectsMissingMessageId) {
    auto const sent_message{
        drogon::sync_wait(bot_at("/bad").SendPrivateMsg({ .user_id = TARGET_USER, .message = hi_message() }))
    };
    REQUIRE(!sent_message.has_value());
    CHECK(sent_message.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(sent_message.error().message.contains("message_id"));
}

DROGON_TEST(SendMsgPassesActionError) {
    auto const sent_message{
        drogon::sync_wait(bot_at("/failing").SendGroupMsg({ .group_id = TARGET_GROUP, .message = hi_message() }))
    };
    REQUIRE(!sent_message.has_value());
    CHECK(sent_message.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(sent_message.error().message.contains("group muted"));
}

auto main(int argc, char** argv) -> int {
    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    drogon::app().loadConfigFile("config.json");
    serve_fake_napcat();
    return test_support::run_tests_with_app(argc, argv);
}
