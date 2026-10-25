/**
 * @file test_actions.cpp
 * @brief Call actions through `OneBot::CallAction` on a fake OneBot HTTP API served by the same app.
 */
#include <cstdint>
#include <print>
#include <string>

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>

#include <drogon_onebot/onebot_client.hpp>

namespace {

    constexpr std::int64_t SELF_ID{ 10001 }; ///< `user_id` that the fake `get_login_info` returns.
    constexpr int FAILED_RETCODE{ 100 };     ///< `retcode` that the fake `delete_msg` returns.
    constexpr double LISTEN_DELAY{ 0.2 };    ///< Seconds to wait for the listener after the beginning advices.

    /**
     * @brief Build a OneBot v11 response envelope.
     * @param status `ok`, `async`, or `failed`.
     * @param retcode 0 for `ok`, 1 for `async`, otherwise an error code.
     * @param data Response data; null for `async` and `failed`.
     * @param message Error text; empty on success.
     * @return Response carrying the envelope with HTTP 200.
     */
    auto envelope(std::string const& status, int retcode, Json::Value const& data, std::string const& message) -> drogon::HttpResponsePtr {
        Json::Value root{ Json::objectValue };
        root["status"] = status;
        root["retcode"] = retcode;
        root["data"] = data;
        root["message"] = message;
        root["wording"] = message;
        return drogon::HttpResponse::newHttpJsonResponse(root);
    }

    /**
     * @brief Answer `get_login_info` with a fixed account.
     * @param req Incoming request, taken by value so it outlives the caller's frame.
     * @return Task yielding an `ok` envelope.
     */
    auto handle_get_login_info([[maybe_unused]] drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value data{ Json::objectValue };
        data["user_id"] = SELF_ID;
        data["nickname"] = "drogon-onebot";
        co_return envelope("ok", 0, data, "");
    }

    /**
     * @brief Reject every `delete_msg` call.
     * @param req Incoming request, taken by value so it outlives the caller's frame.
     * @return Task yielding a `failed` envelope.
     */
    auto handle_delete_msg([[maybe_unused]] drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
        co_return envelope("failed", FAILED_RETCODE, Json::Value{}, "message not found");
    }

    /**
     * @brief Call a succeeding, a failing, and an unknown action and check the results.
     * @param bot Client created by the OneBotClient plugin, copied so it outlives the caller's frame.
     * @return True if each call returned the expected value or `ErrorKind`.
     */
    auto call_actions(drogon_onebot::OneBot bot) -> drogon::Task<bool> {
        auto const login{ co_await bot.CallAction({ .action = "get_login_info" }) };
        if (!login) {
            std::println(stderr, "get_login_info failed: {}", login.error().message);
            co_return false;
        }
        if ((*login)["user_id"].asInt64() != SELF_ID) {
            std::println(stderr, "unexpected get_login_info data: {}", login->toStyledString());
            co_return false;
        }
        std::println("get_login_info: user_id {}, nickname {}", (*login)["user_id"].asInt64(), (*login)["nickname"].asString());

        Json::Value params{ Json::objectValue };
        params["message_id"] = 1;
        auto const failed{ co_await bot.CallAction({ .action = "delete_msg", .params = params }) };
        if (failed || failed.error().kind != drogon_onebot::ErrorKind::Action || failed.error().retcode != FAILED_RETCODE) {
            std::println(stderr, "delete_msg did not fail with an Action error and retcode {}", FAILED_RETCODE);
            co_return false;
        }
        std::println("delete_msg rejected: {}", failed.error().message);

        auto const unknown{ co_await bot.CallAction({ .action = "no_such_action" }) };
        if (unknown || unknown.error().kind != drogon_onebot::ErrorKind::Status || unknown.error().status_code != 404) {
            std::println(stderr, "no_such_action did not fail with HTTP 404");
            co_return false;
        }
        std::println("no_such_action rejected: HTTP {}", unknown.error().status_code);
        co_return true;
    }

} // namespace

auto main() -> int {
    int exit_code{ 1 };
    drogon::app()
        .loadConfigFile("config.json")
        .registerHandler("/get_login_info", &handle_get_login_info, { drogon::Post })
        .registerHandler("/delete_msg", &handle_delete_msg, { drogon::Post })
        .registerBeginningAdvice([&exit_code] {
            drogon::async_run([&exit_code]() -> drogon::Task<> {
                // Beginning advices run before startListening(), and each IO loop starts
                // listening asynchronously, so a request sent right away is refused.
                co_await drogon::sleepCoro(drogon::app().getLoop(), LISTEN_DELAY);
                bool const ok{
                    co_await call_actions(drogon::app().getPlugin<drogon_onebot::OneBotClient>()->NewOneBotClient())
                };
                exit_code = ok ? 0 : 1;
                drogon::app().quit();
            });
        })
        .run();
    return exit_code;
}
