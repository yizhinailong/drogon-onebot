/**
 * @file test_events_unsigned.cpp
 * @brief Post events to an event route without a secret, which accepts them unsigned.
 *
 * Loads config.json without its `secret_env`. Runs offline.
 */
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {

    test_support::Inbox<drogon_onebot::MessageEvent> g_messages; ///< Every message event the handler received.

    /**
     * @brief Build a minimal private message event.
     * @param text Text used as `raw_message` and as the single segment.
     * @return Compact JSON body.
     */
    auto private_message_body(std::string const& text) -> std::string {
        Json::Value event{ Json::objectValue };
        event["post_type"] = "message";
        event["message_type"] = "private";
        event["user_id"] = 20002;
        Json::Value segment{ Json::objectValue };
        segment["type"] = "text";
        segment["data"]["text"] = text;
        event["message"].append(segment);
        event["raw_message"] = text;
        return test_support::to_body(event);
    }

    /**
     * @brief Register the handler that fills `g_messages`.
     *
     * Runs as a beginning advice, before the listeners start.
     */
    auto register_handler() -> void {
        drogon::app().getPlugin<drogon_onebot::OneBotClient>()->OnMessage([](drogon_onebot::MessageEvent event) -> drogon::Task<> {
            g_messages.Push(std::move(event));
            co_return;
        });
    }

} // namespace

DROGON_TEST(HasNoSecret) {
    CHECK(drogon::app().getPlugin<drogon_onebot::OneBotClient>()->Config().secret.empty());
}

DROGON_TEST(AcceptsUnsignedEvent) {
    CHECK(test_support::post_event(private_message_body("unsigned"))->getStatusCode() == drogon::k204NoContent);
    auto const event{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(event.has_value());
    CHECK(event->raw_message == "unsigned");
    CHECK(event->user_id == 20002);
}

DROGON_TEST(IgnoresSignatureWithoutSecret) {
    auto const response{ test_support::post_event(private_message_body("any signature"), "sha1=not-a-mac") };
    CHECK(response->getStatusCode() == drogon::k204NoContent);
    auto const event{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(event.has_value());
    CHECK(event->raw_message == "any signature");
}

auto main(int argc, char** argv) -> int {
    auto config{ test_support::read_config() };
    config["plugins"][0]["config"].removeMember("secret_env");
    drogon::app().loadConfigJson(std::move(config)).registerBeginningAdvice(register_handler);
    return test_support::run_tests_with_app(argc, argv);
}
