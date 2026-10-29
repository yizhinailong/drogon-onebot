/**
 * @file test_events.cpp
 * @brief Post NapCat-style events to the plugin's signed event route and check what the handlers receive.
 *
 * config.json sets `event_path` and `secret_env`, so every event here must carry a valid
 * `X-Signature`. Runs offline.
 */
#include <cctype>
#include <chrono>
#include <cstddef>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/coroutine.h>
#include <trantor/utils/Utilities.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {

    constexpr std::string_view MULTI_MARKER{ "multi" }; ///< `raw_message` that the second message handler records.
    constexpr std::string_view THROW_MARKER{ "throw" }; ///< `raw_message` that makes the second message handler throw.
    constexpr std::string_view SLOW_MARKER{ "slow" };   ///< `raw_message` that makes the second message handler sleep.
    constexpr double SLOW_HANDLER_DELAY{ 1.0 };         ///< Seconds that the second message handler sleeps for `SLOW_MARKER`.

    /**
     * @brief Body that NapCat would post for a private message, and its signature under `SECRET`.
     *
     * The signature was computed independently with Python:
     * `hmac.new(b"drogon-onebot-test-secret", body.encode(), hashlib.sha1).hexdigest()`.
     */
    constexpr std::string_view NAPCAT_BODY{
        R"({"self_id":10001,"user_id":20002,"time":1700000000,"message_id":7,"message_seq":7,"real_id":7,)" R"("message_type":"private","sender":{"user_id":20002,"nickname":"alice","card":""},)" R"("raw_message":"hello","font":14,"sub_type":"friend",)" R"("message":[{"type":"text","data":{"text":"hello"}}],"message_format":"array","post_type":"message"})"
    };
    constexpr std::string_view NAPCAT_SIGNATURE{ "sha1=787d0f82113db205254f6fc3f29701d46bc3ccda" }; ///< `X-Signature` of `NAPCAT_BODY`.

    test_support::Inbox<drogon_onebot::MessageEvent> g_messages;                                    ///< Every message event, from the first handler.
    test_support::Inbox<drogon_onebot::MessageEvent> g_multi_messages;                              ///< `MULTI_MARKER` messages, from the second handler.
    test_support::Inbox<drogon_onebot::MessageEvent> g_slow_messages;                               ///< `SLOW_MARKER` messages, after the second handler slept.
    test_support::Inbox<drogon_onebot::NoticeEvent> g_notices;                                      ///< Every notice event.
    test_support::Inbox<drogon_onebot::RequestEvent> g_requests;                                    ///< Every request event.
    test_support::Inbox<drogon_onebot::MetaEvent> g_meta_events;                                    ///< Every meta event.

    /**
     * @brief Compute HMAC-SHA1 (RFC 2104) as hex, independently of the library's private copy.
     * @param key Secret key.
     * @param message Signed bytes.
     * @return 40 lowercase hex digits.
     */
    auto hmac_sha1_hex(std::string_view key, std::string_view message) -> std::string {
        constexpr std::size_t BLOCK_SIZE{ 64 };
        std::string block{ key };
        if (block.size() > BLOCK_SIZE) {
            auto const digest{ trantor::utils::sha1(block) };
            block.assign(reinterpret_cast<char const*>(digest.bytes), sizeof(digest.bytes));
        }
        block.resize(BLOCK_SIZE, '\0');

        std::string inner{};
        std::string outer{};
        for (auto const byte : block) {
            inner.push_back(static_cast<char>(byte ^ 0x36));
            outer.push_back(static_cast<char>(byte ^ 0x5c));
        }
        inner.append(message);
        auto const inner_digest{ trantor::utils::sha1(inner) };
        outer.append(reinterpret_cast<char const*>(inner_digest.bytes), sizeof(inner_digest.bytes));
        auto const digest{ trantor::utils::sha1(outer) };

        std::string hex{};
        for (auto const byte : digest.bytes) {
            std::format_to(std::back_inserter(hex), "{:02x}", byte);
        }
        return hex;
    }

    /**
     * @brief Sign an event body with `SECRET`, as NapCat's HTTP client does with its `token`.
     * @param body Raw request body.
     * @return `X-Signature` value: `sha1=` and the hex MAC.
     */
    auto sign(std::string const& body) -> std::string {
        return "sha1=" + hmac_sha1_hex(test_support::SECRET, body);
    }

    /**
     * @brief Serialize, sign, and post an event.
     * @param event Event object.
     * @return The route's response.
     */
    auto post_signed(Json::Value const& event) -> drogon::HttpResponsePtr {
        auto const body{ test_support::to_body(event) };
        return test_support::post_event(body, sign(body));
    }

    /**
     * @brief Build the fields that every event has.
     * @param post_type `message`, `notice`, `request`, or `meta_event`.
     * @return Object with `time`, `self_id`, and `post_type`.
     */
    auto base_event(char const* post_type) -> Json::Value {
        Json::Value event{ Json::objectValue };
        event["time"] = 1700000000;
        event["self_id"] = 10001;
        event["post_type"] = post_type;
        return event;
    }

    /**
     * @brief Build a private text message event.
     * @param text Text of the single segment, also used as `raw_message`.
     * @return Message event from user 20002.
     */
    auto private_message(std::string_view text) -> Json::Value {
        auto event{ base_event("message") };
        event["message_type"] = "private";
        event["sub_type"] = "friend";
        event["message_id"] = 8;
        event["user_id"] = 20002;
        Json::Value segment{ Json::objectValue };
        segment["type"] = "text";
        segment["data"]["text"] = std::string{ text };
        event["message"].append(segment);
        event["raw_message"] = std::string{ text };
        event["font"] = 14;
        event["sender"]["user_id"] = 20002;
        event["sender"]["nickname"] = "alice";
        return event;
    }

    /**
     * @brief Check that the first message handler received nothing.
     * @return True if no message event arrived within `NO_EVENT_WAIT`.
     */
    auto no_message_received() -> bool {
        return !g_messages.Pop(test_support::NO_EVENT_WAIT).has_value();
    }

    /**
     * @brief Check that the notice handler received nothing.
     * @return True if no notice event arrived within `NO_EVENT_WAIT`.
     */
    auto no_notice_received() -> bool {
        return !g_notices.Pop(test_support::NO_EVENT_WAIT).has_value();
    }

    /**
     * @brief Build a notice that NapCat would post for a group.
     * @param notice_type The notice's `notice_type`.
     * @return Notice in group 30003 about user 20002.
     */
    auto group_notice(char const* notice_type) -> Json::Value {
        auto event{ base_event("notice") };
        event["notice_type"] = notice_type;
        event["group_id"] = 30003;
        event["user_id"] = 20002;
        return event;
    }

    /**
     * @brief Register the handlers that fill the inboxes.
     *
     * Runs as a beginning advice, before the listeners start.
     */
    auto register_handlers() -> void {
        auto* const plugin{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() };
        plugin->OnMessage([](drogon_onebot::MessageEvent event) -> drogon::Task<> {
            g_messages.Push(std::move(event));
            co_return;
        });
        plugin->OnMessage([](drogon_onebot::MessageEvent event) -> drogon::Task<> {
            if (event.raw_message == THROW_MARKER) {
                throw std::runtime_error{ "handler failure on purpose" };
            }
            if (event.raw_message == MULTI_MARKER) {
                g_multi_messages.Push(event);
            }
            if (event.raw_message == SLOW_MARKER) {
                co_await drogon::sleepCoro(trantor::EventLoop::getEventLoopOfCurrentThread(), SLOW_HANDLER_DELAY);
                g_slow_messages.Push(event);
            }
        });
        plugin->OnNotice([](drogon_onebot::NoticeEvent event) -> drogon::Task<> {
            g_notices.Push(std::move(event));
            co_return;
        });
        plugin->OnRequest([](drogon_onebot::RequestEvent event) -> drogon::Task<> {
            g_requests.Push(std::move(event));
            co_return;
        });
        plugin->OnMetaEvent([](drogon_onebot::MetaEvent event) -> drogon::Task<> {
            g_meta_events.Push(std::move(event));
            co_return;
        });
    }

} // namespace

DROGON_TEST(HmacSha1MatchesRfc2202) {
    CHECK(hmac_sha1_hex(std::string(20, '\x0b'), "Hi There") == "b617318655057264e28bc0b6fb378c8ef146be00");
    CHECK(hmac_sha1_hex("Jefe", "what do ya want for nothing?") == "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
    CHECK(hmac_sha1_hex(std::string(80, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First") == "aa4ae5e15272d00e95705637ce8a3b55ed402112");
}

DROGON_TEST(AcceptsNapCatSignedMessage) {
    auto const response{ test_support::post_event(std::string{ NAPCAT_BODY }, std::string{ NAPCAT_SIGNATURE }) };
    CHECK(response->getStatusCode() == drogon::k204NoContent);

    auto const event{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(event.has_value());
    CHECK(event->time == 1700000000);
    CHECK(event->self_id == 10001);
    CHECK(event->message_type == "private");
    CHECK(event->sub_type == "friend");
    CHECK(event->message_id == 7);
    CHECK(event->user_id == 20002);
    CHECK(!event->group_id.has_value());
    REQUIRE(event->message.size() == 1);
    CHECK(event->message[0].type == "text");
    CHECK(event->message[0].data["text"].asString() == "hello");
    CHECK(event->raw_message == "hello");
    CHECK(event->font == 14);
    CHECK(event->sender.user_id == 20002);
    CHECK(event->sender.nickname == "alice");
    CHECK(event->raw["message_format"].asString() == "array");
}

DROGON_TEST(AcceptsUppercaseSignature) {
    auto const body{ test_support::to_body(private_message("upper")) };
    auto signature{ sign(body) };
    for (auto& digit : signature) {
        digit = static_cast<char>(std::toupper(static_cast<unsigned char>(digit)));
    }
    signature.replace(0, 5, "sha1=");
    CHECK(test_support::post_event(body, signature)->getStatusCode() == drogon::k204NoContent);
    auto const event{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(event.has_value());
    CHECK(event->raw_message == "upper");
}

DROGON_TEST(ParsesGroupMessageWithStringIds) {
    auto event_json{ private_message("group hello") };
    event_json["message_type"] = "group";
    event_json["sub_type"] = "normal";
    event_json["user_id"] = "20002";
    event_json["group_id"] = "30003";
    event_json["sender"]["card"] = "Alice";
    event_json["sender"]["role"] = "admin";
    Json::Value at{ Json::objectValue };
    at["type"] = "at";
    at["data"]["qq"] = "10001";
    event_json["message"].insert(0, at);
    CHECK(post_signed(event_json)->getStatusCode() == drogon::k204NoContent);

    auto const event{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(event.has_value());
    CHECK(event->message_type == "group");
    CHECK(event->user_id == 20002);
    CHECK(event->group_id == 30003);
    CHECK(event->sender.card == "Alice");
    CHECK(event->sender.role == "admin");
    REQUIRE(event->message.size() == 2);
    CHECK(event->message[0].type == "at");
    CHECK(event->message[0].data["qq"].asString() == "10001");
    CHECK(event->message[1].type == "text");
}

DROGON_TEST(RejectsMissingSignature) {
    auto const response{ test_support::post_event(test_support::to_body(private_message("unsigned"))) };
    CHECK(response->getStatusCode() == drogon::k401Unauthorized);
    CHECK(no_message_received());
}

DROGON_TEST(RejectsWrongSignature) {
    auto const body{ test_support::to_body(private_message("tampered")) };
    auto const other_body{ test_support::to_body(private_message("original")) };
    CHECK(test_support::post_event(body, sign(other_body))->getStatusCode() == drogon::k403Forbidden);
    CHECK(test_support::post_event(body, "sha1=" + hmac_sha1_hex("wrong-secret", body))->getStatusCode() == drogon::k403Forbidden);
    CHECK(test_support::post_event(body, hmac_sha1_hex(test_support::SECRET, body))->getStatusCode() == drogon::k403Forbidden);
    CHECK(no_message_received());
}

DROGON_TEST(RejectsBodyThatIsNotJson) {
    std::string const body{ "not json" };
    auto const response{ test_support::post_event(body, sign(body)) };
    CHECK(response->getStatusCode() == drogon::k400BadRequest);
    CHECK(no_message_received());
}

DROGON_TEST(RejectsMalformedEvents) {
    auto cq_string{ private_message("cq") };
    cq_string["message"] = "[CQ:face,id=1]";
    auto const string_format{ post_signed(cq_string) };
    CHECK(string_format->getStatusCode() == drogon::k400BadRequest);
    CHECK(std::string{ string_format->body() }.contains("messagePostFormat"));

    auto no_post_type{ private_message("no post_type") };
    no_post_type.removeMember("post_type");
    CHECK(post_signed(no_post_type)->getStatusCode() == drogon::k400BadRequest);

    auto no_group_id{ private_message("no group_id") };
    no_group_id["message_type"] = "group";
    CHECK(post_signed(no_group_id)->getStatusCode() == drogon::k400BadRequest);

    auto bad_user_id{ private_message("bad user_id") };
    bad_user_id["user_id"] = "abc";
    auto const bad_id{ post_signed(bad_user_id) };
    CHECK(bad_id->getStatusCode() == drogon::k400BadRequest);
    CHECK(std::string{ bad_id->body() }.contains("user_id"));

    auto bad_sender{ private_message("bad sender") };
    bad_sender["sender"]["nickname"] = 5;
    CHECK(post_signed(bad_sender)->getStatusCode() == drogon::k400BadRequest);

    CHECK(post_signed(Json::Value{ Json::arrayValue })->getStatusCode() == drogon::k400BadRequest);
    CHECK(no_message_received());
}

DROGON_TEST(DispatchesNoticeRequestAndMetaEvents) {
    auto notice_json{ group_notice("group_card") };
    notice_json["card_new"] = "alice";
    notice_json["card_old"] = "";
    CHECK(post_signed(notice_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    CHECK(notice->notice_type == "group_card");
    CHECK(notice->sub_type.empty());
    CHECK(notice->self_id == 10001);
    CHECK(std::holds_alternative<std::monostate>(notice->detail));
    CHECK(notice->raw["card_new"].asString() == "alice");

    auto request_json{ base_event("request") };
    request_json["request_type"] = "friend";
    request_json["user_id"] = 20002;
    request_json["comment"] = "hi";
    request_json["flag"] = "flag-1";
    CHECK(post_signed(request_json)->getStatusCode() == drogon::k204NoContent);
    auto const request{ g_requests.Pop(test_support::EVENT_WAIT) };
    REQUIRE(request.has_value());
    CHECK(request->request_type == "friend");
    CHECK(request->sub_type.empty());
    CHECK(request->user_id == 20002);
    CHECK(request->comment == "hi");
    CHECK(request->flag == "flag-1");
    CHECK(!request->group_id.has_value());

    request_json.removeMember("flag");
    auto const no_flag{ post_signed(request_json) };
    CHECK(no_flag->getStatusCode() == drogon::k400BadRequest);
    CHECK(std::string{ no_flag->body() }.contains("flag is missing"));
    CHECK(!g_requests.Pop(test_support::NO_EVENT_WAIT).has_value());

    auto meta_json{ base_event("meta_event") };
    meta_json["meta_event_type"] = "heartbeat";
    meta_json["interval"] = 30000;
    meta_json["status"]["online"] = true;
    CHECK(post_signed(meta_json)->getStatusCode() == drogon::k204NoContent);
    auto const meta{ g_meta_events.Pop(test_support::EVENT_WAIT) };
    REQUIRE(meta.has_value());
    CHECK(meta->meta_event_type == "heartbeat");
    CHECK(meta->raw["interval"].asInt() == 30000);

    auto no_type{ base_event("notice") };
    CHECK(post_signed(no_type)->getStatusCode() == drogon::k400BadRequest);
}

DROGON_TEST(ParsesGroupUploadNotice) {
    auto upload_json{ group_notice("group_upload") };
    upload_json["file"]["id"] = "Ae7bVzY2Pq";
    upload_json["file"]["name"] = "report.pdf";
    upload_json["file"]["size"] = 1048576;
    upload_json["file"]["busid"] = 102;
    CHECK(post_signed(upload_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    auto const* const upload{ std::get_if<drogon_onebot::GroupUploadNotice>(&notice->detail) };
    REQUIRE(upload != nullptr);
    CHECK(upload->group_id == 30003);
    CHECK(upload->user_id == 20002);
    CHECK(upload->file.id == "Ae7bVzY2Pq");
    CHECK(upload->file.name == "report.pdf");
    CHECK(upload->file.size == 1048576);
    CHECK(upload->file.busid == 102);
}

DROGON_TEST(ParsesGroupAdminNotice) {
    auto admin_json{ group_notice("group_admin") };
    admin_json["sub_type"] = "set";
    CHECK(post_signed(admin_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    CHECK(notice->sub_type == "set");
    auto const* const admin{ std::get_if<drogon_onebot::GroupAdminNotice>(&notice->detail) };
    REQUIRE(admin != nullptr);
    CHECK(admin->group_id == 30003);
    CHECK(admin->user_id == 20002);
}

DROGON_TEST(ParsesGroupDecreaseNotice) {
    auto decrease_json{ group_notice("group_decrease") };
    decrease_json["sub_type"] = "kick";
    decrease_json["operator_id"] = 40004;
    CHECK(post_signed(decrease_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    auto const* const decrease{ std::get_if<drogon_onebot::GroupDecreaseNotice>(&notice->detail) };
    REQUIRE(decrease != nullptr);
    CHECK(decrease->group_id == 30003);
    CHECK(decrease->operator_id == 40004);
    CHECK(decrease->user_id == 20002);

    decrease_json.removeMember("operator_id");
    auto const response{ post_signed(decrease_json) };
    CHECK(response->getStatusCode() == drogon::k400BadRequest);
    CHECK(std::string{ response->body() }.contains("operator_id is missing"));
    CHECK(no_notice_received());
}

DROGON_TEST(ParsesGroupIncreaseNotice) {
    auto increase_json{ group_notice("group_increase") };
    increase_json["sub_type"] = "invite";
    increase_json["operator_id"] = 0;
    CHECK(post_signed(increase_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    CHECK(notice->sub_type == "invite");
    auto const* const increase{ std::get_if<drogon_onebot::GroupIncreaseNotice>(&notice->detail) };
    REQUIRE(increase != nullptr);
    CHECK(increase->group_id == 30003);
    CHECK(increase->operator_id == 0);
    CHECK(increase->user_id == 20002);
}

DROGON_TEST(ParsesGroupBanNotice) {
    auto ban_json{ group_notice("group_ban") };
    ban_json["sub_type"] = "ban";
    ban_json["operator_id"] = 40004;
    ban_json["duration"] = 600;
    CHECK(post_signed(ban_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    auto const* const ban{ std::get_if<drogon_onebot::GroupBanNotice>(&notice->detail) };
    REQUIRE(ban != nullptr);
    CHECK(ban->group_id == 30003);
    CHECK(ban->operator_id == 40004);
    CHECK(ban->user_id == 20002);
    CHECK(ban->duration == 600);

    ban_json["user_id"] = 0;
    ban_json["duration"] = -1;
    CHECK(post_signed(ban_json)->getStatusCode() == drogon::k204NoContent);
    auto const whole{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(whole.has_value());
    auto const* const whole_ban{ std::get_if<drogon_onebot::GroupBanNotice>(&whole->detail) };
    REQUIRE(whole_ban != nullptr);
    CHECK(whole_ban->user_id == 0);
    CHECK(whole_ban->duration == -1);
}

DROGON_TEST(ParsesFriendAddNotice) {
    auto add_json{ base_event("notice") };
    add_json["notice_type"] = "friend_add";
    add_json["user_id"] = 20002;
    CHECK(post_signed(add_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    auto const* const added{ std::get_if<drogon_onebot::FriendAddNotice>(&notice->detail) };
    REQUIRE(added != nullptr);
    CHECK(added->user_id == 20002);
}

DROGON_TEST(ParsesGroupRecallNotice) {
    auto recall_json{ group_notice("group_recall") };
    recall_json["operator_id"] = 40004;
    recall_json["message_id"] = -2147000001;
    CHECK(post_signed(recall_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    auto const* const recall{ std::get_if<drogon_onebot::GroupRecallNotice>(&notice->detail) };
    REQUIRE(recall != nullptr);
    CHECK(recall->group_id == 30003);
    CHECK(recall->user_id == 20002);
    CHECK(recall->operator_id == 40004);
    CHECK(recall->message_id == -2147000001);
}

DROGON_TEST(ParsesFriendRecallNotice) {
    auto recall_json{ base_event("notice") };
    recall_json["notice_type"] = "friend_recall";
    recall_json["user_id"] = 20002;
    recall_json["message_id"] = 9;
    CHECK(post_signed(recall_json)->getStatusCode() == drogon::k204NoContent);
    auto const notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(notice.has_value());
    auto const* const recall{ std::get_if<drogon_onebot::FriendRecallNotice>(&notice->detail) };
    REQUIRE(recall != nullptr);
    CHECK(recall->user_id == 20002);
    CHECK(recall->message_id == 9);

    recall_json["message_id"] = "nine";
    auto const response{ post_signed(recall_json) };
    CHECK(response->getStatusCode() == drogon::k400BadRequest);
    CHECK(std::string{ response->body() }.contains("message_id is not an integer"));
    CHECK(no_notice_received());
}

DROGON_TEST(ParsesPokeNotices) {
    auto group_json{ group_notice("notify") };
    group_json["sub_type"] = "poke";
    group_json["target_id"] = 10001;
    CHECK(post_signed(group_json)->getStatusCode() == drogon::k204NoContent);
    auto const group{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(group.has_value());
    auto const* const group_poke{ std::get_if<drogon_onebot::PokeNotice>(&group->detail) };
    REQUIRE(group_poke != nullptr);
    CHECK(group_poke->group_id == 30003);
    CHECK(group_poke->user_id == 20002);
    CHECK(!group_poke->sender_id.has_value());
    CHECK(group_poke->target_id == 10001);

    auto friend_json{ base_event("notice") };
    friend_json["notice_type"] = "notify";
    friend_json["sub_type"] = "poke";
    friend_json["user_id"] = 20002;
    friend_json["sender_id"] = 20002;
    friend_json["target_id"] = 10001;
    CHECK(post_signed(friend_json)->getStatusCode() == drogon::k204NoContent);
    auto const friend_notice{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(friend_notice.has_value());
    auto const* const friend_poke{ std::get_if<drogon_onebot::PokeNotice>(&friend_notice->detail) };
    REQUIRE(friend_poke != nullptr);
    CHECK(!friend_poke->group_id.has_value());
    CHECK(friend_poke->sender_id == 20002);

    auto title_json{ group_notice("notify") };
    title_json["sub_type"] = "title";
    title_json["title"] = "dragon";
    CHECK(post_signed(title_json)->getStatusCode() == drogon::k204NoContent);
    auto const title{ g_notices.Pop(test_support::EVENT_WAIT) };
    REQUIRE(title.has_value());
    CHECK(std::holds_alternative<std::monostate>(title->detail));
}

DROGON_TEST(RejectsNoticeWithoutTypedField) {
    auto upload_json{ group_notice("group_upload") };
    upload_json["file"]["id"] = "Ae7bVzY2Pq";
    auto const response{ post_signed(upload_json) };
    CHECK(response->getStatusCode() == drogon::k400BadRequest);
    CHECK(std::string{ response->body() }.contains("file.name is missing"));
    CHECK(no_notice_received());
}

DROGON_TEST(ParsesGroupRequest) {
    auto request_json{ base_event("request") };
    request_json["request_type"] = "group";
    request_json["sub_type"] = "invite";
    request_json["group_id"] = 30003;
    request_json["user_id"] = 20002;
    request_json["comment"] = "";
    request_json["flag"] = "1761700000000000";
    CHECK(post_signed(request_json)->getStatusCode() == drogon::k204NoContent);
    auto const request{ g_requests.Pop(test_support::EVENT_WAIT) };
    REQUIRE(request.has_value());
    CHECK(request->sub_type == "invite");
    CHECK(request->group_id == 30003);
    CHECK(request->user_id == 20002);
    CHECK(request->comment.empty());
    CHECK(request->flag == "1761700000000000");

    request_json.removeMember("group_id");
    auto const response{ post_signed(request_json) };
    CHECK(response->getStatusCode() == drogon::k400BadRequest);
    CHECK(std::string{ response->body() }.contains("group_id is missing"));
    CHECK(!g_requests.Pop(test_support::NO_EVENT_WAIT).has_value());
}

DROGON_TEST(DropsUnknownPostType) {
    auto sent_json{ private_message("sent by the bot") };
    sent_json["post_type"] = "message_sent";
    CHECK(post_signed(sent_json)->getStatusCode() == drogon::k204NoContent);
    CHECK(no_message_received());
}

DROGON_TEST(RunsEveryHandler) {
    CHECK(post_signed(private_message(MULTI_MARKER))->getStatusCode() == drogon::k204NoContent);
    auto const first{ g_messages.Pop(test_support::EVENT_WAIT) };
    auto const second{ g_multi_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->raw_message == MULTI_MARKER);
    CHECK(second->raw_message == MULTI_MARKER);
}

DROGON_TEST(SurvivesThrowingHandler) {
    CHECK(post_signed(private_message(THROW_MARKER))->getStatusCode() == drogon::k204NoContent);
    auto const thrown{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(thrown.has_value());
    CHECK(thrown->raw_message == THROW_MARKER);

    CHECK(post_signed(private_message("after throw"))->getStatusCode() == drogon::k204NoContent);
    auto const after{ g_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(after.has_value());
    CHECK(after->raw_message == "after throw");
}

DROGON_TEST(AnswersBeforeSlowHandlerFinishes) {
    auto const start{ std::chrono::steady_clock::now() };
    auto const response{ post_signed(private_message(SLOW_MARKER)) };
    auto const elapsed{ std::chrono::steady_clock::now() - start };
    CHECK(response->getStatusCode() == drogon::k204NoContent);
    CHECK(elapsed < std::chrono::milliseconds{ 500 });
    CHECK(!g_slow_messages.Pop(std::chrono::milliseconds{ 0 }).has_value());

    CHECK(g_messages.Pop(test_support::EVENT_WAIT).has_value());
    auto const slow{ g_slow_messages.Pop(test_support::EVENT_WAIT) };
    REQUIRE(slow.has_value());
    CHECK(slow->raw_message == SLOW_MARKER);
}

auto main(int argc, char** argv) -> int {
    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    drogon::app().loadConfigFile("config.json").registerBeginningAdvice(register_handlers);
    return test_support::run_tests_with_app(argc, argv);
}
