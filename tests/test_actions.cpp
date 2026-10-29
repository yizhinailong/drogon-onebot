/**
 * @file test_actions.cpp
 * @brief Call actions on a fake OneBot HTTP API that the same app serves, NapCat-style.
 *
 * Covers `CallAction` (request shape, `Authorization`, the `base_url` path prefix, and
 * every `ErrorKind`) and the typed actions, with the `data` shapes of NapCat's source. Runs offline.
 */
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <utility>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/coroutine.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {

    constexpr std::int64_t SELF_ID{ 10001 };                       ///< `user_id` of the fake `get_login_info`.
    constexpr std::int64_t PREFIX_SELF_ID{ 10002 };                ///< `user_id` of the fake `get_login_info` under `/prefix`.
    constexpr std::int64_t TARGET_USER{ 20002 };                   ///< `user_id` that the fake `send_private_msg` accepts.
    constexpr std::int64_t TARGET_GROUP{ 30003 };                  ///< `group_id` that the fake `send_group_msg` accepts.
    constexpr std::int32_t PRIVATE_MESSAGE_ID{ 42 };               ///< `message_id` of the fake `send_private_msg`.
    constexpr std::int32_t GROUP_MESSAGE_ID{ 43 };                 ///< `message_id` of the fake `send_group_msg`.
    constexpr std::int32_t STORED_MESSAGE_ID{ 44 };                ///< `message_id` that the fake `delete_msg` and `get_msg` know.
    constexpr std::int64_t MESSAGE_TIME{ 1761700000 };             ///< `time` of the message that the fake `get_msg` returns.
    constexpr char const* FORWARD_ID{ "7561700000000000001" };     ///< `id` of the forward that the fake `get_forward_msg` knows, a NapCat message ID.
    constexpr char const* COOKIES{ "uin=o10001; skey=@abc12345" }; ///< `cookies` of the fake `get_cookies`.
    constexpr std::int32_t CSRF_TOKEN{ 123456789 };                ///< CSRF token of the fake `get_csrf_token` and `get_credentials`, and `bkn` of `get_cookies`.
    constexpr char const* RECORD_FILE{ "8C1B4E7B.amr" };           ///< `file` of the voice that the fake `get_record` knows, a NapCat file name.
    constexpr char const* RECORD_PATH{ "/ptt/8C1B4E7B.amr" };      ///< Path of `RECORD_FILE` on the fake NapCat's host.
    constexpr char const* IMAGE_FILE{ "6B4DE3DF.jpg" };            ///< `file` of the image that the fake `get_image` knows, a NapCat file name.
    constexpr char const* IMAGE_PATH{ "/pic/6B4DE3DF.jpg" };       ///< Path of `IMAGE_FILE` on the fake NapCat's host.
    constexpr int FAILED_RETCODE{ 100 };                           ///< `retcode` of the fake actions that fail.
    constexpr double SLOW_DELAY{ 1.0 };                            ///< Seconds that the fake `slow` action takes.

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
        return { drogon_onebot::MessageSegment::Text("hi") };
    }

    /**
     * @brief Return the JSON body of a request to a fake action.
     * @param req Request to the fake action.
     * @return The body, or null when it is not JSON.
     */
    auto body_of(drogon::HttpRequestPtr const& req) -> Json::Value {
        return req->getJsonObject() != nullptr ? *req->getJsonObject() : Json::Value{};
    }

    /**
     * @brief Build NapCat's `get_msg` data for a group message, extensions included.
     * @return Data of a message from `TARGET_USER` in `TARGET_GROUP` that mentions `SELF_ID`.
     */
    auto stored_group_message() -> Json::Value {
        Json::Value sender{ Json::objectValue };
        sender["user_id"] = TARGET_USER;
        sender["nickname"] = "alice";
        sender["card"] = "Alice";
        sender["role"] = "admin";

        Json::Value at{ Json::objectValue };
        at["type"] = "at";
        at["data"]["qq"] = std::to_string(SELF_ID);
        Json::Value text{ Json::objectValue };
        text["type"] = "text";
        text["data"]["text"] = " hi";
        Json::Value message{ Json::arrayValue };
        message.append(at);
        message.append(text);

        Json::Value data{ Json::objectValue };
        data["self_id"] = SELF_ID;
        data["user_id"] = TARGET_USER;
        data["time"] = MESSAGE_TIME;
        data["message_id"] = STORED_MESSAGE_ID;
        data["message_seq"] = STORED_MESSAGE_ID;
        data["real_id"] = STORED_MESSAGE_ID;
        data["real_seq"] = "8848";
        data["message_type"] = "group";
        data["sub_type"] = "normal";
        data["sender"] = sender;
        data["raw_message"] = "[CQ:at,qq=10001] hi";
        data["font"] = 14;
        data["message"] = message;
        data["message_format"] = "array";
        data["post_type"] = "message";
        data["group_id"] = TARGET_GROUP;
        data["group_name"] = "test group";
        data["emoji_likes_list"] = Json::Value{ Json::arrayValue };
        return data;
    }

    /**
     * @brief Build NapCat's `get_msg` data for a private message.
     * @return `stored_group_message()` without the group fields and the sender's `role`.
     */
    auto stored_private_message() -> Json::Value {
        auto data{ stored_group_message() };
        data["message_type"] = "private";
        data["sub_type"] = "friend";
        data.removeMember("group_id");
        data.removeMember("group_name");
        data["sender"].removeMember("role");
        return data;
    }

    /**
     * @brief Build NapCat's `get_forward_msg` data, with a nested forward inline as NapCat sends it.
     * @return Object whose `messages` are `stored_group_message()` and a private message
     * that holds a `forward` segment with `stored_group_message()` in its `content`.
     */
    auto forwarded_messages() -> Json::Value {
        Json::Value nested{ Json::objectValue };
        nested["type"] = "forward";
        nested["data"]["id"] = "7561700000000000002";
        nested["data"]["content"].append(stored_group_message());
        auto outer{ stored_private_message() };
        outer["message"] = Json::Value{ Json::arrayValue };
        outer["message"].append(nested);

        Json::Value data{ Json::objectValue };
        data["messages"].append(stored_group_message());
        data["messages"].append(outer);
        return data;
    }

    /**
     * @brief Build NapCat's `get_group_info` data, plus the request body under `request`.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return Data of `TARGET_GROUP`.
     */
    auto group_info(Json::Value const& request) -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["group_all_shut"] = 0;
        data["group_remark"] = "";
        data["group_id"] = TARGET_GROUP;
        data["group_name"] = "test group";
        data["member_count"] = 42;
        data["max_member_count"] = 500;
        data["request"] = request;
        return data;
    }

    /**
     * @brief Build NapCat's `get_group_list` data, plus the request body under `request` in each entry.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return `group_info()` of `TARGET_GROUP`, then a second group with its ID as a string.
     */
    auto group_list(Json::Value const& request) -> Json::Value {
        auto other{ group_info(request) };
        other["group_id"] = std::to_string(TARGET_GROUP + 1);
        other["group_name"] = "other group";
        Json::Value data{ Json::arrayValue };
        data.append(group_info(request));
        data.append(other);
        return data;
    }

    /**
     * @brief Build NapCat's `get_group_member_info` data, plus the request body under `request`.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return Data of `TARGET_USER` in `TARGET_GROUP`.
     */
    auto group_member_info(Json::Value const& request) -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["group_id"] = TARGET_GROUP;
        data["user_id"] = TARGET_USER;
        data["nickname"] = "alice";
        data["card"] = "Alice";
        data["sex"] = "female";
        data["age"] = 0;
        data["area"] = "";
        data["level"] = "5";
        data["qq_level"] = 30;
        data["join_time"] = 1700000000;
        data["last_sent_time"] = MESSAGE_TIME;
        data["title_expire_time"] = 0;
        data["unfriendly"] = false;
        data["card_changeable"] = true;
        data["is_robot"] = false;
        data["shut_up_timestamp"] = 0;
        data["role"] = "admin";
        data["title"] = "";
        data["request"] = request;
        return data;
    }

    /**
     * @brief Build NapCat's `get_group_member_list` data, plus the request body under `request` in each entry.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return `group_member_info()` of `TARGET_USER`, then the owner `SELF_ID` without `role`,
     * as NapCat leaves it out when it does not know the member's role.
     */
    auto group_member_list(Json::Value const& request) -> Json::Value {
        auto owner{ group_member_info(request) };
        owner["user_id"] = SELF_ID;
        owner["nickname"] = "drogon-onebot";
        owner.removeMember("role");
        Json::Value data{ Json::arrayValue };
        data.append(group_member_info(request));
        data.append(owner);
        return data;
    }

    /**
     * @brief Build one honor holder as NapCat's `get_group_honor_info` lists it.
     * @param user_id QQ number, sent as a string as NapCat passes QQ's `uin` through.
     * @param nickname Nickname.
     * @param description Description of the honor.
     * @return Object with `user_id`, `nickname`, `avatar`, and `description`.
     */
    auto honor_member(
        std::int64_t user_id,
        char const* nickname,
        char const* description
    ) -> Json::Value {
        Json::Value entry{ Json::objectValue };
        entry["user_id"] = std::to_string(user_id);
        entry["nickname"] = nickname;
        entry["avatar"] = "https://example.invalid/avatar.png";
        entry["description"] = description;
        return entry;
    }

    /**
     * @brief Build NapCat's `get_group_honor_info` data, plus the request body under `request`.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return Honors of `TARGET_GROUP`: `TARGET_USER` as the most talkative member and as a
     * performer, and empty lists otherwise, as NapCat always sends every list.
     */
    auto group_honor_info(Json::Value const& request) -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["group_id"] = TARGET_GROUP;
        data["current_talkative"] = honor_member(TARGET_USER, "alice", "talkative for 3 days");
        data["talkative_list"].append(honor_member(TARGET_USER, "alice", "talkative for 3 days"));
        data["performer_list"].append(honor_member(TARGET_USER, "alice", "performer"));
        for (auto const* key : { "legend_list", "emotion_list", "strong_newbie_list" }) {
            data[key] = Json::Value{ Json::arrayValue };
        }
        data["request"] = request;
        return data;
    }

    /**
     * @brief Build NapCat's `get_stranger_info` data, plus the request body under `request`.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return Data of `TARGET_USER`.
     */
    auto stranger_info(Json::Value const& request) -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["user_id"] = TARGET_USER;
        data["uid"] = "u_alice";
        data["uin"] = std::to_string(TARGET_USER);
        data["nickname"] = "alice";
        data["age"] = 20;
        data["qid"] = "";
        data["qqLevel"] = 30;
        data["sex"] = "female";
        data["long_nick"] = "hello";
        data["reg_time"] = 1500000000;
        data["is_vip"] = false;
        data["login_days"] = 0;
        data["request"] = request;
        return data;
    }

    /**
     * @brief Build NapCat's `get_friend_list` data, plus the request body under `request` in each entry.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return `TARGET_USER` with a remark, then `SELF_ID + 1` without one, as NapCat builds them.
     */
    auto friend_list(Json::Value const& request) -> Json::Value {
        Json::Value data{ Json::arrayValue };
        for (auto const& [user_id, nickname, remark] : {
                 std::tuple{ TARGET_USER, "alice", "Alice from work" },
                 std::tuple{ SELF_ID + 1, "bob", "" },
             }) {
            Json::Value entry{ Json::objectValue };
            entry["birthday_year"] = 2000;
            entry["birthday_month"] = 1;
            entry["birthday_day"] = 1;
            entry["user_id"] = user_id;
            entry["age"] = 26;
            entry["phone_num"] = "";
            entry["email"] = "";
            entry["category_id"] = 0;
            entry["nickname"] = nickname;
            entry["remark"] = remark;
            entry["sex"] = "unknown";
            entry["level"] = 30;
            entry["request"] = request;
            data.append(entry);
        }
        return data;
    }

    /**
     * @brief Build NapCat's `get_cookies` data, plus the request body under `request`.
     * @param request Body of the request, echoed so tests can check what was sent.
     * @return `COOKIES` with NapCat's `bkn`, the CSRF token computed from their `skey`.
     */
    auto cookies_data(Json::Value const& request) -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["cookies"] = COOKIES;
        data["bkn"] = std::to_string(CSRF_TOKEN);
        data["request"] = request;
        return data;
    }

    /**
     * @brief Build NapCat's `get_credentials` data.
     * @return `COOKIES` with `CSRF_TOKEN` under NapCat's `token`, not the standard's `csrf_token`.
     */
    auto credentials_data() -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["cookies"] = COOKIES;
        data["token"] = CSRF_TOKEN;
        return data;
    }

    /**
     * @brief Answer as NapCat's payload check does when a required parameter is missing.
     * @return `failed` with retcode 400.
     */
    auto missing_parameter() -> drogon::HttpResponsePtr {
        return envelope("failed", 400, Json::Value{}, "Schema compilation error: Expected required property");
    }

    /**
     * @brief Answer a fake cookie action, whose `domain` NapCat's payload check requires.
     * @param req Request to the fake action.
     * @param data Data to answer with when `domain` is a string.
     * @return `ok` with `data`, or `missing_parameter()` without `domain`.
     */
    auto domain_or_failed(
        drogon::HttpRequestPtr const& req,
        Json::Value const& data
    ) -> drogon::HttpResponsePtr {
        if (!body_of(req)["domain"].isString()) {
            return missing_parameter();
        }
        return ok(data);
    }

    /**
     * @brief Answer the fake `get_record` as NapCat does for `RECORD_FILE`.
     *
     * NapCat's payload check requires `out_format`. An empty one answers with the original
     * file; another converts it to `<path>.<out_format>` and adds the content as `base64`.
     * @param req Request to the fake action.
     * @return `ok` with the file, or `failed` for a missing `out_format` or an unknown `file`.
     */
    auto converted_record(drogon::HttpRequestPtr const& req) -> drogon::HttpResponsePtr {
        auto const body{ body_of(req) };
        if (!body["out_format"].isString()) {
            return missing_parameter();
        }
        if (body["file"].asString() != RECORD_FILE) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "file not found");
        }
        Json::Value data{ Json::objectValue };
        data["file"] = RECORD_PATH;
        data["url"] = RECORD_PATH;
        data["file_size"] = "2048";
        data["file_name"] = RECORD_FILE;
        auto const format{ body["out_format"].asString() };
        if (!format.empty()) {
            data["file"] = std::string{ RECORD_PATH } + "." + format;
            data["url"] = data["file"];
            data["base64"] = "SUQzBAAAAAAAIw==";
        }
        return ok(data);
    }

    /**
     * @brief Build NapCat's `get_image` data for `IMAGE_FILE`, as `enableLocalFile2Url` off leaves it.
     * @return The local path, QQ's URL of the image, and its size and name, without `base64`.
     */
    auto image_file() -> Json::Value {
        Json::Value data{ Json::objectValue };
        data["file"] = IMAGE_PATH;
        data["url"] = "https://multimedia.nt.qq.com.cn/download?appid=1407&fileid=abc";
        data["file_size"] = "1024";
        data["file_name"] = IMAGE_FILE;
        return data;
    }

    /**
     * @brief Answer a fake action that checks one ID parameter.
     * @param req Request to the fake action.
     * @param key Name of the ID parameter.
     * @param expected Value that `key` must have.
     * @param data Data to answer with when it does.
     * @return `ok` with `data`, or `failed` with `FAILED_RETCODE`, as NapCat answers an unknown ID.
     */
    auto known_id_or_failed(
        drogon::HttpRequestPtr const& req,
        char const* key,
        std::int64_t expected,
        Json::Value const& data
    ) -> drogon::HttpResponsePtr {
        if (body_of(req)[key].asInt64() != expected) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, std::string{ "unknown " } + key);
        }
        return ok(data);
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
     * @brief Check the parameters of `send_msg`, choosing the target as NapCat does.
     *
     * Without `message_type`, NapCat sends to `group_id` when it is set and to `user_id`
     * otherwise. Unlike NapCat, this rejects any member sent with an empty or unknown value.
     * @param req Request to the fake action.
     * @return What `check_send` answers for the chosen target, or `failed` when there is none.
     */
    auto check_send_msg(drogon::HttpRequestPtr const& req) -> drogon::HttpResponsePtr {
        auto const body{ body_of(req) };
        auto const type{ body["message_type"].isString() ? body["message_type"].asString() : "" };
        if (body.isMember("message_type") && type != "private" && type != "group") {
            return envelope("failed", 1400, Json::Value{}, "wrong message_type");
        }
        for (auto const& [key, expected] : { std::pair{ "user_id", TARGET_USER }, std::pair{ "group_id", TARGET_GROUP } }) {
            if (body.isMember(key) && (!body[key].isIntegral() || body[key].asInt64() != expected)) {
                return envelope("failed", 1400, Json::Value{}, std::string{ "wrong " } + key);
            }
        }
        if (type == "group" || (type.empty() && body.isMember("group_id"))) {
            return check_send(req, "group_id", TARGET_GROUP, GROUP_MESSAGE_ID);
        }
        if (body.isMember("user_id")) {
            return check_send(req, "user_id", TARGET_USER, PRIVATE_MESSAGE_ID);
        }
        return envelope("failed", FAILED_RETCODE, Json::Value{}, "no group_id or user_id");
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
     * @brief Return the requests that the fake setters received, for tests to read back.
     * @return Inbox of `{ "action": <name>, "body": <request body> }` objects, oldest first.
     */
    auto setter_requests() -> test_support::Inbox<Json::Value>& {
        static test_support::Inbox<Json::Value> requests;
        return requests;
    }

    /**
     * @brief Serve a fake setter, which answers `ok` with null `data` as NapCat's setters do.
     *
     * `/<action>` records each request in `setter_requests()`, and `/failing/<action>`
     * answers `failed` with `FAILED_RETCODE`.
     * @param action Action name.
     * @param failure Error text of the failing variant, like NapCat's for that action.
     */
    auto serve_setter(
        std::string const& action,
        std::string failure
    ) -> void {
        serve("/" + action, [action](drogon::HttpRequestPtr const& req) {
            Json::Value request{ Json::objectValue };
            request["action"] = action;
            request["body"] = body_of(req);
            setter_requests().Push(std::move(request));
            return ok(Json::Value{});
        });
        serve("/failing/" + action, [failure = std::move(failure)](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, failure);
        });
    }

    /**
     * @brief Take the request that the last setter call sent.
     * @param action Action that should have received it.
     * @return Its body, or null when no request to `action` arrived.
     */
    auto sent_to(std::string const& action) -> Json::Value {
        auto const request{ setter_requests().Pop(test_support::EVENT_WAIT) };
        if (!request || (*request)["action"].asString() != action) {
            return Json::Value{};
        }
        return (*request)["body"];
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
            data["body"] = body_of(req);
            data["authorization"] = req->getHeader("authorization");
            return ok(data);
        });
        serve("/async_action", [](auto const&) { return envelope("async", 1, Json::Value{}, ""); });
        serve("/delete_msg", [](drogon::HttpRequestPtr const& req) {
            return known_id_or_failed(req, "message_id", STORED_MESSAGE_ID, Json::Value{});
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
        serve("/send_msg", check_send_msg);
        serve("/bad/get_login_info", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["nickname"] = "no user_id";
            return ok(data);
        });
        serve("/bad/send_private_msg", [](auto const&) { return ok(Json::Value{ Json::objectValue }); });
        serve("/failing/send_group_msg", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "group muted");
        });
        serve("/get_msg", [](drogon::HttpRequestPtr const& req) {
            return known_id_or_failed(req, "message_id", STORED_MESSAGE_ID, stored_group_message());
        });
        serve("/private/get_msg", [](auto const&) { return ok(stored_private_message()); });
        serve("/string_format/get_msg", [](auto const&) {
            auto data{ stored_group_message() };
            data["message"] = data["raw_message"];
            data["message_format"] = "string";
            return ok(data);
        });
        serve("/bad/get_msg", [](auto const&) {
            auto data{ stored_group_message() };
            data["sender"] = Json::Value{ Json::objectValue };
            return ok(data);
        });
        serve("/get_forward_msg", [](drogon::HttpRequestPtr const& req) {
            auto const body{ body_of(req) };
            if (!body["id"].isString() || body["id"].asString() != FORWARD_ID) {
                return envelope("failed", FAILED_RETCODE, Json::Value{}, "unknown id");
            }
            return ok(forwarded_messages());
        });
        serve("/bad/get_forward_msg", [](auto const&) {
            auto data{ forwarded_messages() };
            data["messages"][1]["sender"] = Json::Value{ Json::objectValue };
            return ok(data);
        });
        serve("/standard/get_forward_msg", [](auto const&) {
            Json::Value text{ Json::objectValue };
            text["type"] = "text";
            text["data"]["text"] = "hi";
            Json::Value node{ Json::objectValue };
            node["type"] = "node";
            node["data"]["user_id"] = std::to_string(TARGET_USER);
            node["data"]["nickname"] = "alice";
            node["data"]["content"].append(text);
            Json::Value data{ Json::objectValue };
            data["message"].append(node);
            return ok(data);
        });
        serve("/get_group_info", [](drogon::HttpRequestPtr const& req) {
            return known_id_or_failed(req, "group_id", TARGET_GROUP, group_info(body_of(req)));
        });
        serve("/bad/get_group_info", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["group_id"] = TARGET_GROUP;
            return ok(data);
        });
        serve("/get_group_list", [](drogon::HttpRequestPtr const& req) { return ok(group_list(body_of(req))); });
        serve("/bad/get_group_list", [](drogon::HttpRequestPtr const& req) {
            auto data{ group_list(body_of(req)) };
            data[1].removeMember("group_name");
            return ok(data);
        });
        serve("/failing/get_group_list", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "get group list failed");
        });
        serve("/get_group_member_info", [](drogon::HttpRequestPtr const& req) {
            if (body_of(req)["group_id"].asInt64() != TARGET_GROUP) {
                return envelope("failed", FAILED_RETCODE, Json::Value{}, "unknown group_id");
            }
            return known_id_or_failed(req, "user_id", TARGET_USER, group_member_info(body_of(req)));
        });
        serve("/no_role/get_group_member_info", [](drogon::HttpRequestPtr const& req) {
            auto data{ group_member_info(body_of(req)) };
            data.removeMember("role");
            return ok(data);
        });
        serve("/bad/get_group_member_info", [](drogon::HttpRequestPtr const& req) {
            auto data{ group_member_info(body_of(req)) };
            data["unfriendly"] = "no";
            return ok(data);
        });
        serve("/get_group_member_list", [](drogon::HttpRequestPtr const& req) {
            return known_id_or_failed(req, "group_id", TARGET_GROUP, group_member_list(body_of(req)));
        });
        serve("/bad/get_group_member_list", [](drogon::HttpRequestPtr const& req) {
            auto data{ group_member_list(body_of(req)) };
            data[1].removeMember("user_id");
            return ok(data);
        });
        serve("/get_group_honor_info", [](drogon::HttpRequestPtr const& req) {
            return known_id_or_failed(req, "group_id", TARGET_GROUP, group_honor_info(body_of(req)));
        });
        serve("/nobody/get_group_honor_info", [](drogon::HttpRequestPtr const& req) {
            auto data{ group_honor_info(body_of(req)) };
            data["current_talkative"] = Json::Value{ Json::objectValue };
            data["talkative_list"] = Json::Value{ Json::arrayValue };
            data.removeMember("performer_list");
            return ok(data);
        });
        serve("/bad/get_group_honor_info", [](drogon::HttpRequestPtr const& req) {
            auto data{ group_honor_info(body_of(req)) };
            data["performer_list"][0].removeMember("nickname");
            return ok(data);
        });
        serve("/get_stranger_info", [](drogon::HttpRequestPtr const& req) {
            return known_id_or_failed(req, "user_id", TARGET_USER, stranger_info(body_of(req)));
        });
        serve("/bad/get_stranger_info", [](auto const&) { return ok(Json::Value{}); });
        serve("/get_friend_list", [](drogon::HttpRequestPtr const& req) { return ok(friend_list(body_of(req))); });
        serve("/bad/get_friend_list", [](drogon::HttpRequestPtr const& req) {
            auto data{ friend_list(body_of(req)) };
            data[1].removeMember("nickname");
            return ok(data);
        });
        serve("/failing/get_friend_list", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "get friend list failed");
        });
        serve_setter("set_group_kick", "get Uid Error");
        serve_setter("set_group_ban", "cannot ban owner");
        serve_setter("set_group_whole_ban", "SetGroupWholeBan failed: ERR_NOT_GROUP_ADMIN");
        serve_setter("set_group_admin", "get Uid Error");
        serve_setter("set_group_card", "group card not applied");
        serve_setter("set_group_name", "set group name failed");
        serve_setter("set_group_leave", "group not found");
        serve_setter("set_group_special_title", "packetBackend unavailable");
        serve_setter("set_friend_add_request", "No such request");
        serve_setter("set_group_add_request", "No such request");
        serve("/send_like", [](drogon::HttpRequestPtr const& req) {
            // Refuse more likes than the standard's daily limit of 10, as QQ would.
            auto const body{ body_of(req) };
            auto const& times{ body["times"] };
            if (body["user_id"].asInt64() != TARGET_USER || !times.isIntegral() || times.asInt() < 1 || times.asInt() > 10) {
                return envelope("failed", FAILED_RETCODE, Json::Value{}, "like failed");
            }
            // NapCat's send_like returns nothing, so its envelope has no data member.
            Json::Value root{ Json::objectValue };
            root["status"] = "ok";
            root["retcode"] = 0;
            root["message"] = "";
            root["wording"] = "";
            return drogon::HttpResponse::newHttpJsonResponse(root);
        });
        serve("/get_cookies", [](drogon::HttpRequestPtr const& req) {
            return domain_or_failed(req, cookies_data(body_of(req)));
        });
        serve("/bad/get_cookies", [](drogon::HttpRequestPtr const& req) {
            auto data{ cookies_data(body_of(req)) };
            data.removeMember("cookies");
            return ok(data);
        });
        serve("/failing/get_cookies", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "get cookies failed");
        });
        serve("/get_csrf_token", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["token"] = CSRF_TOKEN;
            return ok(data);
        });
        serve("/bad/get_csrf_token", [](auto const&) {
            // One past the largest int32, which NapCat's 31-bit hash never yields.
            Json::Value data{ Json::objectValue };
            data["token"] = Json::Int64{ 2147483648 };
            return ok(data);
        });
        serve("/failing/get_csrf_token", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "SKey is Empty");
        });
        serve("/get_credentials", [](drogon::HttpRequestPtr const& req) {
            return domain_or_failed(req, credentials_data());
        });
        serve("/standard/get_credentials", [](auto const&) {
            auto data{ credentials_data() };
            data.removeMember("token");
            data["csrf_token"] = CSRF_TOKEN;
            return ok(data);
        });
        serve("/bad/get_credentials", [](auto const&) {
            auto data{ credentials_data() };
            data.removeMember("token");
            return ok(data);
        });
        serve("/failing/get_credentials", [](auto const&) {
            return envelope("failed", FAILED_RETCODE, Json::Value{}, "get credentials failed");
        });
        serve("/get_record", converted_record);
        serve("/bad/get_record", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["url"] = RECORD_PATH;
            return ok(data);
        });
        serve("/get_image", [](drogon::HttpRequestPtr const& req) {
            if (body_of(req)["file"].asString() != IMAGE_FILE) {
                return envelope("failed", FAILED_RETCODE, Json::Value{}, "file not found");
            }
            return ok(image_file());
        });
        serve("/bad/get_image", [](auto const&) {
            auto data{ image_file() };
            data["file"] = 1;
            return ok(data);
        });
        serve("/can_send_image", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["yes"] = true;
            return ok(data);
        });
        serve("/unable/can_send_image", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["yes"] = false;
            return ok(data);
        });
        serve("/bad/can_send_image", [](auto const&) { return ok(Json::Value{ Json::objectValue }); });
        serve("/can_send_record", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["yes"] = true;
            return ok(data);
        });
        serve("/bad/can_send_record", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["yes"] = "true";
            return ok(data);
        });
        serve("/get_status", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["online"] = true;
            data["good"] = true;
            data["stat"] = Json::Value{ Json::objectValue };
            return ok(data);
        });
        serve("/unknown/get_status", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["online"] = Json::Value{};
            data["good"] = false;
            return ok(data);
        });
        serve("/bad/get_status", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["online"] = true;
            return ok(data);
        });
        serve("/mistyped/get_status", [](auto const&) {
            Json::Value data{ Json::objectValue };
            data["online"] = 1;
            data["good"] = true;
            return ok(data);
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
    CHECK(result.error().message.contains("unknown message_id"));
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
    // Windows retries a refused connect for about 2 s before reporting it, so a
    // shorter timeout would make this a Timeout instead of a Connection error.
    auto const result{ drogon::sync_wait(bot.CallAction({ .action = "get_login_info", .timeout = 10.0 })) };
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

DROGON_TEST(SendPrivateMsgRejectsMissingMessageId) {
    auto const sent_message{
        drogon::sync_wait(bot_at("/bad").SendPrivateMsg({ .user_id = TARGET_USER, .message = hi_message() }))
    };
    REQUIRE(!sent_message.has_value());
    CHECK(sent_message.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(sent_message.error().message.contains("message_id"));
}

DROGON_TEST(SendGroupMsgPassesActionError) {
    auto const sent_message{
        drogon::sync_wait(bot_at("/failing").SendGroupMsg({ .group_id = TARGET_GROUP, .message = hi_message() }))
    };
    REQUIRE(!sent_message.has_value());
    CHECK(sent_message.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(sent_message.error().message.contains("group muted"));
}

DROGON_TEST(SendMsgSendsOnlySetTargets) {
    // The fake rejects empty or wrong values, so a member sent while unset fails these.
    auto const to_group{ drogon::sync_wait(plugin_bot().SendMsg({ .group_id = TARGET_GROUP, .message = hi_message() })) };
    REQUIRE(to_group.has_value());
    CHECK(to_group->message_id == GROUP_MESSAGE_ID);

    auto const to_user{ drogon::sync_wait(plugin_bot().SendMsg({ .user_id = TARGET_USER, .message = hi_message() })) };
    REQUIRE(to_user.has_value());
    CHECK(to_user->message_id == PRIVATE_MESSAGE_ID);
}

DROGON_TEST(SendMsgSendsMessageType) {
    // With both IDs, the fake sends to the group unless message_type says otherwise.
    auto const sent_message{ drogon::sync_wait(plugin_bot().SendMsg({
        .message_type = "private",
        .user_id = TARGET_USER,
        .group_id = TARGET_GROUP,
        .message = hi_message(),
    })) };
    REQUIRE(sent_message.has_value());
    CHECK(sent_message->message_id == PRIVATE_MESSAGE_ID);
}

DROGON_TEST(SendMsgPassesMissingTargetError) {
    auto const sent_message{ drogon::sync_wait(plugin_bot().SendMsg({ .message = hi_message() })) };
    REQUIRE(!sent_message.has_value());
    CHECK(sent_message.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(sent_message.error().message.contains("no group_id or user_id"));
}

DROGON_TEST(DeleteMsgAcceptsNullData) {
    auto const deleted{ drogon::sync_wait(plugin_bot().DeleteMsg({ .message_id = STORED_MESSAGE_ID })) };
    CHECK(deleted.has_value());
}

DROGON_TEST(DeleteMsgPassesActionError) {
    auto const deleted{ drogon::sync_wait(plugin_bot().DeleteMsg({ .message_id = STORED_MESSAGE_ID + 1 })) };
    REQUIRE(!deleted.has_value());
    CHECK(deleted.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(deleted.error().retcode == FAILED_RETCODE);
}

DROGON_TEST(GetMsgReadsGroupMessage) {
    auto const info{ drogon::sync_wait(plugin_bot().GetMsg({ .message_id = STORED_MESSAGE_ID })) };
    REQUIRE(info.has_value());
    CHECK(info->time == MESSAGE_TIME);
    CHECK(info->message_type == "group");
    CHECK(info->message_id == STORED_MESSAGE_ID);
    CHECK(info->real_id == STORED_MESSAGE_ID);
    CHECK(info->group_id == TARGET_GROUP);
    CHECK(info->sender.user_id == TARGET_USER);
    CHECK(info->sender.nickname == "alice");
    CHECK(info->sender.card == "Alice");
    CHECK(info->sender.role == "admin");
    REQUIRE(info->message.size() == 2);
    CHECK(info->message[0].type == "at");
    CHECK(info->message[0].data["qq"].asString() == std::to_string(SELF_ID));
    CHECK(info->message[1].data["text"].asString() == " hi");
    CHECK(info->raw_message == "[CQ:at,qq=10001] hi");
    CHECK(info->raw["group_name"].asString() == "test group");
}

DROGON_TEST(GetMsgReadsPrivateMessage) {
    auto const info{ drogon::sync_wait(bot_at("/private").GetMsg({ .message_id = STORED_MESSAGE_ID })) };
    REQUIRE(info.has_value());
    CHECK(info->message_type == "private");
    CHECK(!info->group_id.has_value());
    CHECK(info->sender.user_id == TARGET_USER);
    CHECK(info->sender.role.empty());
}

DROGON_TEST(GetMsgPassesActionError) {
    auto const info{ drogon::sync_wait(plugin_bot().GetMsg({ .message_id = STORED_MESSAGE_ID + 1 })) };
    REQUIRE(!info.has_value());
    CHECK(info.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(info.error().message.contains("unknown message_id"));
}

DROGON_TEST(GetMsgRejectsStringFormat) {
    auto const info{ drogon::sync_wait(bot_at("/string_format").GetMsg({ .message_id = STORED_MESSAGE_ID })) };
    REQUIRE(!info.has_value());
    CHECK(info.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(info.error().message.contains("messagePostFormat"));
}

DROGON_TEST(GetMsgRejectsSenderWithoutUserId) {
    auto const info{ drogon::sync_wait(bot_at("/bad").GetMsg({ .message_id = STORED_MESSAGE_ID })) };
    REQUIRE(!info.has_value());
    CHECK(info.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(info.error().message.contains("sender.user_id"));
}

DROGON_TEST(GetForwardMsgReadsMessages) {
    auto const forward{ drogon::sync_wait(plugin_bot().GetForwardMsg({ .id = FORWARD_ID })) };
    REQUIRE(forward.has_value());
    REQUIRE(forward->messages.size() == 2);
    auto const& first{ forward->messages[0] };
    CHECK(first.message_id == STORED_MESSAGE_ID);
    CHECK(first.group_id == TARGET_GROUP);
    CHECK(first.sender.nickname == "alice");
    REQUIRE(first.message.size() == 2);
    CHECK(first.message[1].data["text"].asString() == " hi");

    auto const& outer{ forward->messages[1] };
    CHECK(outer.message_type == "private");
    REQUIRE(outer.message.size() == 1);
    CHECK(outer.message[0].type == "forward");
    CHECK(outer.message[0].data["content"][0]["message_id"].asInt() == STORED_MESSAGE_ID);
}

DROGON_TEST(GetForwardMsgPassesActionError) {
    auto const forward{ drogon::sync_wait(plugin_bot().GetForwardMsg({ .id = "1" })) };
    REQUIRE(!forward.has_value());
    CHECK(forward.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(forward.error().message.contains("unknown id"));
}

DROGON_TEST(GetForwardMsgRejectsInvalidMessage) {
    auto const forward{ drogon::sync_wait(bot_at("/bad").GetForwardMsg({ .id = FORWARD_ID })) };
    REQUIRE(!forward.has_value());
    CHECK(forward.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(forward.error().message.contains("messages[1].sender.user_id is missing"));
}

DROGON_TEST(GetForwardMsgRejectsStandardShape) {
    // The standard answers with `message` node segments; NapCat, which this reads, with `messages`.
    auto const forward{ drogon::sync_wait(bot_at("/standard").GetForwardMsg({ .id = FORWARD_ID })) };
    REQUIRE(!forward.has_value());
    CHECK(forward.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(forward.error().message.contains("messages is missing"));
}

DROGON_TEST(GetStrangerInfoReadsProfile) {
    auto const info{ drogon::sync_wait(plugin_bot().GetStrangerInfo({ .user_id = TARGET_USER })) };
    REQUIRE(info.has_value());
    CHECK(info->user_id == TARGET_USER);
    CHECK(info->nickname == "alice");
    CHECK(info->sex == "female");
    CHECK(info->age == 20);
    CHECK(info->raw["long_nick"].asString() == "hello");
}

DROGON_TEST(GetStrangerInfoRejectsNullData) {
    auto const info{ drogon::sync_wait(bot_at("/bad").GetStrangerInfo({ .user_id = TARGET_USER })) };
    REQUIRE(!info.has_value());
    CHECK(info.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(info.error().message.contains("user_id"));
}

DROGON_TEST(SendLikeSendsUserAndTimes) {
    auto const once{ drogon::sync_wait(plugin_bot().SendLike({ .user_id = TARGET_USER })) };
    CHECK(once.has_value());

    auto const ten{ drogon::sync_wait(plugin_bot().SendLike({ .user_id = TARGET_USER, .times = 10 })) };
    CHECK(ten.has_value());
}

DROGON_TEST(SendLikePassesActionError) {
    auto const liked{ drogon::sync_wait(plugin_bot().SendLike({ .user_id = TARGET_USER, .times = 11 })) };
    REQUIRE(!liked.has_value());
    CHECK(liked.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(liked.error().message.contains("like failed"));
}

DROGON_TEST(GetFriendListReadsFriends) {
    auto const friends{ drogon::sync_wait(plugin_bot().GetFriendList()) };
    REQUIRE(friends.has_value());
    REQUIRE(friends->size() == 2);
    CHECK((*friends)[0].user_id == TARGET_USER);
    CHECK((*friends)[0].nickname == "alice");
    CHECK((*friends)[0].remark == "Alice from work");
    CHECK((*friends)[0].raw["level"].asInt() == 30);
    CHECK((*friends)[1].user_id == SELF_ID + 1);
    CHECK((*friends)[1].remark.empty());
}

DROGON_TEST(GetFriendListRejectsInvalidEntry) {
    auto const friends{ drogon::sync_wait(bot_at("/bad").GetFriendList()) };
    REQUIRE(!friends.has_value());
    CHECK(friends.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(friends.error().message.contains("data[1].nickname is missing"));
}

DROGON_TEST(GetFriendListPassesActionError) {
    auto const friends{ drogon::sync_wait(bot_at("/failing").GetFriendList()) };
    REQUIRE(!friends.has_value());
    CHECK(friends.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(friends.error().message.contains("get friend list failed"));
}

DROGON_TEST(GetGroupInfoReadsGroup) {
    auto const info{ drogon::sync_wait(plugin_bot().GetGroupInfo({ .group_id = TARGET_GROUP })) };
    REQUIRE(info.has_value());
    CHECK(info->group_id == TARGET_GROUP);
    CHECK(info->group_name == "test group");
    CHECK(info->member_count == 42);
    CHECK(info->max_member_count == 500);
    CHECK(info->raw["group_remark"].isString());
}

DROGON_TEST(GetGroupInfoRejectsMissingName) {
    auto const info{ drogon::sync_wait(bot_at("/bad").GetGroupInfo({ .group_id = TARGET_GROUP })) };
    REQUIRE(!info.has_value());
    CHECK(info.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(info.error().message.contains("group_name"));
}

DROGON_TEST(GetGroupListReadsGroups) {
    auto const groups{ drogon::sync_wait(plugin_bot().GetGroupList()) };
    REQUIRE(groups.has_value());
    REQUIRE(groups->size() == 2);
    CHECK((*groups)[0].group_id == TARGET_GROUP);
    CHECK((*groups)[0].group_name == "test group");
    CHECK((*groups)[0].member_count == 42);
    CHECK((*groups)[1].group_id == TARGET_GROUP + 1);
    CHECK((*groups)[1].group_name == "other group");
}

DROGON_TEST(GetGroupListRejectsInvalidEntry) {
    auto const groups{ drogon::sync_wait(bot_at("/bad").GetGroupList()) };
    REQUIRE(!groups.has_value());
    CHECK(groups.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(groups.error().message.contains("data[1].group_name is missing"));
}

DROGON_TEST(GetGroupListPassesActionError) {
    auto const groups{ drogon::sync_wait(bot_at("/failing").GetGroupList()) };
    REQUIRE(!groups.has_value());
    CHECK(groups.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(groups.error().message.contains("get group list failed"));
}

DROGON_TEST(GetGroupMemberInfoReadsMember) {
    auto const info{
        drogon::sync_wait(plugin_bot().GetGroupMemberInfo({ .group_id = TARGET_GROUP, .user_id = TARGET_USER }))
    };
    REQUIRE(info.has_value());
    CHECK(info->group_id == TARGET_GROUP);
    CHECK(info->user_id == TARGET_USER);
    CHECK(info->nickname == "alice");
    CHECK(info->card == "Alice");
    CHECK(info->sex == "female");
    CHECK(info->level == "5");
    CHECK(info->join_time == 1700000000);
    CHECK(info->last_sent_time == MESSAGE_TIME);
    CHECK(info->role == "admin");
    CHECK(!info->unfriendly);
    CHECK(info->card_changeable);
    CHECK(info->raw["is_robot"].isBool());
}

DROGON_TEST(GetGroupMemberInfoAllowsMissingRole) {
    auto const info{
        drogon::sync_wait(bot_at("/no_role").GetGroupMemberInfo({ .group_id = TARGET_GROUP, .user_id = TARGET_USER }))
    };
    REQUIRE(info.has_value());
    CHECK(info->role.empty());
}

DROGON_TEST(GetGroupMemberInfoRejectsWrongType) {
    auto const info{
        drogon::sync_wait(bot_at("/bad").GetGroupMemberInfo({ .group_id = TARGET_GROUP, .user_id = TARGET_USER }))
    };
    REQUIRE(!info.has_value());
    CHECK(info.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(info.error().message.contains("unfriendly is not a boolean"));
}

DROGON_TEST(GetGroupMemberListReadsMembers) {
    auto const members{ drogon::sync_wait(plugin_bot().GetGroupMemberList({ .group_id = TARGET_GROUP })) };
    REQUIRE(members.has_value());
    REQUIRE(members->size() == 2);
    CHECK((*members)[0].group_id == TARGET_GROUP);
    CHECK((*members)[0].user_id == TARGET_USER);
    CHECK((*members)[0].card == "Alice");
    CHECK((*members)[0].role == "admin");
    CHECK((*members)[1].user_id == SELF_ID);
    CHECK((*members)[1].role.empty());
}

DROGON_TEST(GetGroupMemberListRejectsInvalidEntry) {
    auto const members{ drogon::sync_wait(bot_at("/bad").GetGroupMemberList({ .group_id = TARGET_GROUP })) };
    REQUIRE(!members.has_value());
    CHECK(members.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(members.error().message.contains("data[1].user_id is missing"));
}

DROGON_TEST(GetGroupMemberListPassesActionError) {
    auto const members{ drogon::sync_wait(plugin_bot().GetGroupMemberList({ .group_id = TARGET_GROUP + 1 })) };
    REQUIRE(!members.has_value());
    CHECK(members.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(members.error().message.contains("unknown group_id"));
}

DROGON_TEST(GetGroupHonorInfoReadsHonors) {
    auto const honors{ drogon::sync_wait(plugin_bot().GetGroupHonorInfo({ .group_id = TARGET_GROUP })) };
    REQUIRE(honors.has_value());
    CHECK(honors->group_id == TARGET_GROUP);
    REQUIRE(honors->current_talkative.has_value());
    CHECK(honors->current_talkative->user_id == TARGET_USER);
    CHECK(honors->current_talkative->nickname == "alice");
    CHECK(honors->current_talkative->day_count == 0);
    REQUIRE(honors->talkative_list.size() == 1);
    CHECK(honors->talkative_list[0].description == "talkative for 3 days");
    REQUIRE(honors->performer_list.size() == 1);
    CHECK(honors->performer_list[0].user_id == TARGET_USER);
    CHECK(honors->legend_list.empty());
    CHECK(honors->strong_newbie_list.empty());
    CHECK(honors->raw["request"]["type"].asString() == "all");

    auto const talkative{ drogon::sync_wait(plugin_bot().GetGroupHonorInfo({ .group_id = TARGET_GROUP, .type = "talkative" })) };
    REQUIRE(talkative.has_value());
    CHECK(talkative->raw["request"]["type"].asString() == "talkative");
}

DROGON_TEST(GetGroupHonorInfoAllowsNobodyAndMissingLists) {
    auto const honors{ drogon::sync_wait(bot_at("/nobody").GetGroupHonorInfo({ .group_id = TARGET_GROUP })) };
    REQUIRE(honors.has_value());
    CHECK(!honors->current_talkative.has_value());
    CHECK(honors->talkative_list.empty());
    CHECK(honors->performer_list.empty());
}

DROGON_TEST(GetGroupHonorInfoRejectsInvalidEntry) {
    auto const honors{ drogon::sync_wait(bot_at("/bad").GetGroupHonorInfo({ .group_id = TARGET_GROUP })) };
    REQUIRE(!honors.has_value());
    CHECK(honors.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(honors.error().message.contains("performer_list[0].nickname is missing"));
}

DROGON_TEST(GetGroupHonorInfoPassesActionError) {
    auto const honors{ drogon::sync_wait(plugin_bot().GetGroupHonorInfo({ .group_id = TARGET_GROUP + 1 })) };
    REQUIRE(!honors.has_value());
    CHECK(honors.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(honors.error().message.contains("unknown group_id"));
}

DROGON_TEST(SetGroupKickSendsRejectFlag) {
    auto const kicked{ drogon::sync_wait(plugin_bot().SetGroupKick({ .group_id = TARGET_GROUP, .user_id = TARGET_USER })) };
    CHECK(kicked.has_value());
    auto const body{ sent_to("set_group_kick") };
    CHECK(body["group_id"].asInt64() == TARGET_GROUP);
    CHECK(body["user_id"].asInt64() == TARGET_USER);
    CHECK(body["reject_add_request"].isBool());
    CHECK(!body["reject_add_request"].asBool());
}

DROGON_TEST(SetGroupKickPassesActionError) {
    auto const kicked{ drogon::sync_wait(bot_at("/failing").SetGroupKick({ .group_id = TARGET_GROUP, .user_id = TARGET_USER })) };
    REQUIRE(!kicked.has_value());
    CHECK(kicked.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(kicked.error().message.contains("get Uid Error"));
}

DROGON_TEST(SetGroupBanSendsDuration) {
    auto const muted{ drogon::sync_wait(plugin_bot().SetGroupBan({ .group_id = TARGET_GROUP, .user_id = TARGET_USER })) };
    CHECK(muted.has_value());
    auto const mute{ sent_to("set_group_ban") };
    CHECK(mute["group_id"].asInt64() == TARGET_GROUP);
    CHECK(mute["user_id"].asInt64() == TARGET_USER);
    CHECK(mute["duration"].isIntegral());
    CHECK(mute["duration"].asInt() == 1800);

    auto const lifted{
        drogon::sync_wait(plugin_bot().SetGroupBan({ .group_id = TARGET_GROUP, .user_id = TARGET_USER, .duration = 0 }))
    };
    CHECK(lifted.has_value());
    auto const lift{ sent_to("set_group_ban") };
    CHECK(lift["duration"].isIntegral());
    CHECK(lift["duration"].asInt() == 0);
}

DROGON_TEST(SetGroupBanPassesActionError) {
    auto const muted{ drogon::sync_wait(bot_at("/failing").SetGroupBan({ .group_id = TARGET_GROUP, .user_id = TARGET_USER })) };
    REQUIRE(!muted.has_value());
    CHECK(muted.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(muted.error().message.contains("cannot ban owner"));
}

DROGON_TEST(SetGroupWholeBanSendsEnable) {
    auto const muted{ drogon::sync_wait(plugin_bot().SetGroupWholeBan({ .group_id = TARGET_GROUP })) };
    CHECK(muted.has_value());
    auto const mute{ sent_to("set_group_whole_ban") };
    CHECK(mute["group_id"].asInt64() == TARGET_GROUP);
    CHECK(mute["enable"].isBool());
    CHECK(mute["enable"].asBool());

    auto const lifted{ drogon::sync_wait(plugin_bot().SetGroupWholeBan({ .group_id = TARGET_GROUP, .enable = false })) };
    CHECK(lifted.has_value());
    auto const lift{ sent_to("set_group_whole_ban") };
    CHECK(lift["enable"].isBool());
    CHECK(!lift["enable"].asBool());
}

DROGON_TEST(SetGroupWholeBanPassesActionError) {
    auto const muted{ drogon::sync_wait(bot_at("/failing").SetGroupWholeBan({ .group_id = TARGET_GROUP })) };
    REQUIRE(!muted.has_value());
    CHECK(muted.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(muted.error().message.contains("ERR_NOT_GROUP_ADMIN"));
}

DROGON_TEST(SetGroupAdminSendsEnable) {
    // NapCat reads a missing enable as false, so the default must be sent.
    auto const promoted{ drogon::sync_wait(plugin_bot().SetGroupAdmin({ .group_id = TARGET_GROUP, .user_id = TARGET_USER })) };
    CHECK(promoted.has_value());
    auto const promote{ sent_to("set_group_admin") };
    CHECK(promote["group_id"].asInt64() == TARGET_GROUP);
    CHECK(promote["user_id"].asInt64() == TARGET_USER);
    CHECK(promote["enable"].isBool());
    CHECK(promote["enable"].asBool());

    auto const demoted{
        drogon::sync_wait(plugin_bot().SetGroupAdmin({ .group_id = TARGET_GROUP, .user_id = TARGET_USER, .enable = false }))
    };
    CHECK(demoted.has_value());
    auto const demote{ sent_to("set_group_admin") };
    CHECK(demote["enable"].isBool());
    CHECK(!demote["enable"].asBool());
}

DROGON_TEST(SetGroupAdminPassesActionError) {
    auto const promoted{
        drogon::sync_wait(bot_at("/failing").SetGroupAdmin({ .group_id = TARGET_GROUP, .user_id = TARGET_USER }))
    };
    REQUIRE(!promoted.has_value());
    CHECK(promoted.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(promoted.error().message.contains("get Uid Error"));
}

DROGON_TEST(SetGroupCardSendsCard) {
    auto const renamed{
        drogon::sync_wait(plugin_bot().SetGroupCard({ .group_id = TARGET_GROUP, .user_id = TARGET_USER, .card = "Alice" }))
    };
    CHECK(renamed.has_value());
    auto const rename{ sent_to("set_group_card") };
    CHECK(rename["group_id"].asInt64() == TARGET_GROUP);
    CHECK(rename["user_id"].asInt64() == TARGET_USER);
    CHECK(rename["card"].asString() == "Alice");

    auto const cleared{ drogon::sync_wait(plugin_bot().SetGroupCard({ .group_id = TARGET_GROUP, .user_id = TARGET_USER })) };
    CHECK(cleared.has_value());
    auto const clear{ sent_to("set_group_card") };
    CHECK(clear["card"].isString());
    CHECK(clear["card"].asString().empty());
}

DROGON_TEST(SetGroupCardPassesActionError) {
    auto const renamed{
        drogon::sync_wait(bot_at("/failing").SetGroupCard({ .group_id = TARGET_GROUP, .user_id = TARGET_USER, .card = "Alice" }))
    };
    REQUIRE(!renamed.has_value());
    CHECK(renamed.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(renamed.error().message.contains("group card not applied"));
}

DROGON_TEST(SetGroupNameSendsName) {
    auto const renamed{ drogon::sync_wait(plugin_bot().SetGroupName({ .group_id = TARGET_GROUP, .group_name = "new name" })) };
    CHECK(renamed.has_value());
    auto const body{ sent_to("set_group_name") };
    CHECK(body["group_id"].asInt64() == TARGET_GROUP);
    CHECK(body["group_name"].asString() == "new name");
}

DROGON_TEST(SetGroupNamePassesActionError) {
    auto const renamed{
        drogon::sync_wait(bot_at("/failing").SetGroupName({ .group_id = TARGET_GROUP, .group_name = "new name" }))
    };
    REQUIRE(!renamed.has_value());
    CHECK(renamed.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(renamed.error().message.contains("set group name failed"));
}

DROGON_TEST(SetGroupLeaveSendsDismissFlag) {
    auto const left{ drogon::sync_wait(plugin_bot().SetGroupLeave({ .group_id = TARGET_GROUP })) };
    CHECK(left.has_value());
    auto const leave{ sent_to("set_group_leave") };
    CHECK(leave["group_id"].asInt64() == TARGET_GROUP);
    CHECK(leave["is_dismiss"].isBool());
    CHECK(!leave["is_dismiss"].asBool());

    auto const disbanded{ drogon::sync_wait(plugin_bot().SetGroupLeave({ .group_id = TARGET_GROUP, .is_dismiss = true })) };
    CHECK(disbanded.has_value());
    auto const disband{ sent_to("set_group_leave") };
    CHECK(disband["is_dismiss"].isBool());
    CHECK(disband["is_dismiss"].asBool());
}

DROGON_TEST(SetGroupLeavePassesActionError) {
    auto const left{ drogon::sync_wait(bot_at("/failing").SetGroupLeave({ .group_id = TARGET_GROUP })) };
    REQUIRE(!left.has_value());
    CHECK(left.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(left.error().message.contains("group not found"));
}

DROGON_TEST(SetGroupSpecialTitleSendsTitleAndDuration) {
    auto const titled{ drogon::sync_wait(
        plugin_bot().SetGroupSpecialTitle({ .group_id = TARGET_GROUP, .user_id = TARGET_USER, .special_title = "helper" })
    ) };
    CHECK(titled.has_value());
    auto const body{ sent_to("set_group_special_title") };
    CHECK(body["group_id"].asInt64() == TARGET_GROUP);
    CHECK(body["user_id"].asInt64() == TARGET_USER);
    CHECK(body["special_title"].asString() == "helper");
    CHECK(body["duration"].isIntegral());
    CHECK(body["duration"].asInt64() == -1);
}

DROGON_TEST(SetGroupSpecialTitlePassesActionError) {
    auto const titled{ drogon::sync_wait(
        bot_at("/failing").SetGroupSpecialTitle({ .group_id = TARGET_GROUP, .user_id = TARGET_USER, .special_title = "helper" })
    ) };
    REQUIRE(!titled.has_value());
    CHECK(titled.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(titled.error().message.contains("packetBackend unavailable"));
}

DROGON_TEST(SetFriendAddRequestSendsRemarkOnlyWhenSet) {
    auto const accepted{ drogon::sync_wait(plugin_bot().SetFriendAddRequest({ .flag = "1761700000" })) };
    CHECK(accepted.has_value());
    auto const accept{ sent_to("set_friend_add_request") };
    CHECK(accept["flag"].asString() == "1761700000");
    CHECK(accept["approve"].isBool());
    CHECK(accept["approve"].asBool());
    CHECK(!accept.isMember("remark"));

    auto const remarked{ drogon::sync_wait(plugin_bot().SetFriendAddRequest({ .flag = "1761700000", .remark = "alice" })) };
    CHECK(remarked.has_value());
    CHECK(sent_to("set_friend_add_request")["remark"].asString() == "alice");

    auto const refused{ drogon::sync_wait(plugin_bot().SetFriendAddRequest({ .flag = "1761700000", .approve = false })) };
    CHECK(refused.has_value());
    auto const refuse{ sent_to("set_friend_add_request") };
    CHECK(refuse["approve"].isBool());
    CHECK(!refuse["approve"].asBool());
}

DROGON_TEST(SetFriendAddRequestPassesActionError) {
    auto const accepted{ drogon::sync_wait(bot_at("/failing").SetFriendAddRequest({ .flag = "1" })) };
    REQUIRE(!accepted.has_value());
    CHECK(accepted.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(accepted.error().message.contains("No such request"));
}

DROGON_TEST(SetGroupAddRequestSendsSubTypeAndReasonOnlyWhenSet) {
    auto const accepted{ drogon::sync_wait(plugin_bot().SetGroupAddRequest({ .flag = "1761700000000000" })) };
    CHECK(accepted.has_value());
    auto const accept{ sent_to("set_group_add_request") };
    CHECK(accept["flag"].asString() == "1761700000000000");
    CHECK(accept["approve"].isBool());
    CHECK(accept["approve"].asBool());
    CHECK(!accept.isMember("sub_type"));
    CHECK(!accept.isMember("reason"));

    auto const refused{ drogon::sync_wait(plugin_bot().SetGroupAddRequest({
        .flag = "1761700000000000",
        .sub_type = "add",
        .approve = false,
        .reason = "group is full",
    })) };
    CHECK(refused.has_value());
    auto const refuse{ sent_to("set_group_add_request") };
    CHECK(refuse["sub_type"].asString() == "add");
    CHECK(refuse["approve"].isBool());
    CHECK(!refuse["approve"].asBool());
    CHECK(refuse["reason"].asString() == "group is full");
}

DROGON_TEST(SetGroupAddRequestPassesActionError) {
    auto const accepted{ drogon::sync_wait(bot_at("/failing").SetGroupAddRequest({ .flag = "1" })) };
    REQUIRE(!accepted.has_value());
    CHECK(accepted.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(accepted.error().message.contains("No such request"));
}

DROGON_TEST(GetCookiesReadsCookies) {
    auto const cookies{ drogon::sync_wait(plugin_bot().GetCookies({ .domain = "qun.qq.com" })) };
    REQUIRE(cookies.has_value());
    CHECK(cookies->cookies == COOKIES);
    CHECK(cookies->raw["bkn"].asString() == std::to_string(CSRF_TOKEN));
    CHECK(cookies->raw["request"]["domain"].asString() == "qun.qq.com");
}

DROGON_TEST(GetCookiesAlwaysSendsDomain) {
    auto const cookies{ drogon::sync_wait(plugin_bot().GetCookies()) };
    REQUIRE(cookies.has_value());
    CHECK(cookies->raw["request"]["domain"].isString());
    CHECK(cookies->raw["request"]["domain"].asString().empty());
}

DROGON_TEST(GetCookiesRejectsMissingCookies) {
    auto const cookies{ drogon::sync_wait(bot_at("/bad").GetCookies({ .domain = "qun.qq.com" })) };
    REQUIRE(!cookies.has_value());
    CHECK(cookies.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(cookies.error().message.contains("cookies is missing"));
}

DROGON_TEST(GetCookiesPassesActionError) {
    auto const cookies{ drogon::sync_wait(bot_at("/failing").GetCookies({ .domain = "qun.qq.com" })) };
    REQUIRE(!cookies.has_value());
    CHECK(cookies.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(cookies.error().message.contains("get cookies failed"));
}

DROGON_TEST(GetCsrfTokenReadsToken) {
    auto const token{ drogon::sync_wait(plugin_bot().GetCsrfToken()) };
    REQUIRE(token.has_value());
    CHECK(token->token == CSRF_TOKEN);
}

DROGON_TEST(GetCsrfTokenRejectsTokenOutOfRange) {
    auto const token{ drogon::sync_wait(bot_at("/bad").GetCsrfToken()) };
    REQUIRE(!token.has_value());
    CHECK(token.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(token.error().message.contains("token is not an integer in range"));
}

DROGON_TEST(GetCsrfTokenPassesActionError) {
    auto const token{ drogon::sync_wait(bot_at("/failing").GetCsrfToken()) };
    REQUIRE(!token.has_value());
    CHECK(token.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(token.error().message.contains("SKey is Empty"));
}

DROGON_TEST(GetCredentialsReadsNapCatToken) {
    auto const credentials{ drogon::sync_wait(plugin_bot().GetCredentials({ .domain = "qun.qq.com" })) };
    REQUIRE(credentials.has_value());
    CHECK(credentials->cookies == COOKIES);
    CHECK(credentials->csrf_token == CSRF_TOKEN);
}

DROGON_TEST(GetCredentialsReadsStandardToken) {
    auto const credentials{ drogon::sync_wait(bot_at("/standard").GetCredentials({ .domain = "qun.qq.com" })) };
    REQUIRE(credentials.has_value());
    CHECK(credentials->cookies == COOKIES);
    CHECK(credentials->csrf_token == CSRF_TOKEN);
}

DROGON_TEST(GetCredentialsAlwaysSendsDomain) {
    auto const credentials{ drogon::sync_wait(plugin_bot().GetCredentials()) };
    CHECK(credentials.has_value());
}

DROGON_TEST(GetCredentialsRejectsMissingToken) {
    auto const credentials{ drogon::sync_wait(bot_at("/bad").GetCredentials({ .domain = "qun.qq.com" })) };
    REQUIRE(!credentials.has_value());
    CHECK(credentials.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(credentials.error().message.contains("csrf_token is missing"));
}

DROGON_TEST(GetCredentialsPassesActionError) {
    auto const credentials{ drogon::sync_wait(bot_at("/failing").GetCredentials({ .domain = "qun.qq.com" })) };
    REQUIRE(!credentials.has_value());
    CHECK(credentials.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(credentials.error().message.contains("get credentials failed"));
}

DROGON_TEST(GetRecordConvertsFormat) {
    auto const record{ drogon::sync_wait(plugin_bot().GetRecord({ .file = RECORD_FILE, .out_format = "mp3" })) };
    REQUIRE(record.has_value());
    CHECK(record->file == std::string{ RECORD_PATH } + ".mp3");
    CHECK(!record->raw["base64"].asString().empty());
}

DROGON_TEST(GetRecordAlwaysSendsOutFormat) {
    auto const record{ drogon::sync_wait(plugin_bot().GetRecord({ .file = RECORD_FILE })) };
    REQUIRE(record.has_value());
    CHECK(record->file == RECORD_PATH);
    CHECK(!record->raw.isMember("base64"));
}

DROGON_TEST(GetRecordRejectsMissingFile) {
    auto const record{ drogon::sync_wait(bot_at("/bad").GetRecord({ .file = RECORD_FILE, .out_format = "mp3" })) };
    REQUIRE(!record.has_value());
    CHECK(record.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(record.error().message.contains("file is missing"));
}

DROGON_TEST(GetRecordPassesActionError) {
    auto const record{ drogon::sync_wait(plugin_bot().GetRecord({ .file = "unknown.amr", .out_format = "mp3" })) };
    REQUIRE(!record.has_value());
    CHECK(record.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(record.error().message.contains("file not found"));
}

DROGON_TEST(GetImageReadsFile) {
    auto const image{ drogon::sync_wait(plugin_bot().GetImage({ .file = IMAGE_FILE })) };
    REQUIRE(image.has_value());
    CHECK(image->file == IMAGE_PATH);
    CHECK(image->raw["url"].asString().starts_with("https://"));
    CHECK(image->raw["file_name"].asString() == IMAGE_FILE);
    CHECK(!image->raw.isMember("base64"));
}

DROGON_TEST(GetImageRejectsMistypedFile) {
    auto const image{ drogon::sync_wait(bot_at("/bad").GetImage({ .file = IMAGE_FILE })) };
    REQUIRE(!image.has_value());
    CHECK(image.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(image.error().message.contains("file is not a string"));
}

DROGON_TEST(GetImagePassesActionError) {
    auto const image{ drogon::sync_wait(plugin_bot().GetImage({ .file = "unknown.jpg" })) };
    REQUIRE(!image.has_value());
    CHECK(image.error().kind == drogon_onebot::ErrorKind::Action);
    CHECK(image.error().message.contains("file not found"));
}

DROGON_TEST(CanSendImageReadsAnswer) {
    auto const able{ drogon::sync_wait(plugin_bot().CanSendImage()) };
    REQUIRE(able.has_value());
    CHECK(able->yes);

    auto const unable{ drogon::sync_wait(bot_at("/unable").CanSendImage()) };
    REQUIRE(unable.has_value());
    CHECK(!unable->yes);
}

DROGON_TEST(CanSendImageRejectsMissingAnswer) {
    auto const able{ drogon::sync_wait(bot_at("/bad").CanSendImage()) };
    REQUIRE(!able.has_value());
    CHECK(able.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(able.error().message.contains("yes is missing"));
}

DROGON_TEST(CanSendRecordReadsAnswer) {
    auto const able{ drogon::sync_wait(plugin_bot().CanSendRecord()) };
    REQUIRE(able.has_value());
    CHECK(able->yes);
}

DROGON_TEST(CanSendRecordRejectsMistypedAnswer) {
    auto const able{ drogon::sync_wait(bot_at("/bad").CanSendRecord()) };
    REQUIRE(!able.has_value());
    CHECK(able.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(able.error().message.contains("yes is not a boolean"));
}

DROGON_TEST(GetStatusReadsStatus) {
    auto const status{ drogon::sync_wait(plugin_bot().GetStatus()) };
    REQUIRE(status.has_value());
    CHECK(status->online == true);
    CHECK(status->good);
    CHECK(status->raw["stat"].isObject());
}

DROGON_TEST(GetStatusReadsNullOnlineAsUnknown) {
    auto const status{ drogon::sync_wait(bot_at("/unknown").GetStatus()) };
    REQUIRE(status.has_value());
    CHECK(!status->online.has_value());
    CHECK(!status->good);
}

DROGON_TEST(GetStatusRejectsInvalidStatus) {
    auto const missing{ drogon::sync_wait(bot_at("/bad").GetStatus()) };
    REQUIRE(!missing.has_value());
    CHECK(missing.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(missing.error().message.contains("good is missing"));

    auto const mistyped{ drogon::sync_wait(bot_at("/mistyped").GetStatus()) };
    REQUIRE(!mistyped.has_value());
    CHECK(mistyped.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    CHECK(mistyped.error().message.contains("online is not a boolean"));
}

DROGON_TEST(NoCacheIsSentOnlyWhenSet) {
    auto const group{ drogon::sync_wait(plugin_bot().GetGroupInfo({ .group_id = TARGET_GROUP })) };
    REQUIRE(group.has_value());
    CHECK(!group->raw["request"].isMember("no_cache"));

    auto const fresh{ drogon::sync_wait(plugin_bot().GetGroupInfo({ .group_id = TARGET_GROUP, .no_cache = true })) };
    REQUIRE(fresh.has_value());
    CHECK(fresh->raw["request"]["no_cache"].isBool());
    CHECK(fresh->raw["request"]["no_cache"].asBool());

    auto const cached{ drogon::sync_wait(
        plugin_bot().GetGroupMemberInfo({ .group_id = TARGET_GROUP, .user_id = TARGET_USER, .no_cache = false })
    ) };
    REQUIRE(cached.has_value());
    CHECK(cached->raw["request"]["no_cache"].isBool());
    CHECK(!cached->raw["request"]["no_cache"].asBool());

    auto const stranger{ drogon::sync_wait(plugin_bot().GetStrangerInfo({ .user_id = TARGET_USER })) };
    REQUIRE(stranger.has_value());
    CHECK(!stranger->raw["request"].isMember("no_cache"));

    auto const friends{ drogon::sync_wait(plugin_bot().GetFriendList()) };
    REQUIRE(friends.has_value());
    REQUIRE(!friends->empty());
    CHECK(!friends->front().raw["request"].isMember("no_cache"));

    auto const fresh_friends{ drogon::sync_wait(plugin_bot().GetFriendList({ .no_cache = true })) };
    REQUIRE(fresh_friends.has_value());
    REQUIRE(!fresh_friends->empty());
    CHECK(fresh_friends->front().raw["request"]["no_cache"].asBool());

    auto const fresh_groups{ drogon::sync_wait(plugin_bot().GetGroupList({ .no_cache = true })) };
    REQUIRE(fresh_groups.has_value());
    REQUIRE(!fresh_groups->empty());
    CHECK(fresh_groups->front().raw["request"]["no_cache"].asBool());

    auto const members{ drogon::sync_wait(plugin_bot().GetGroupMemberList({ .group_id = TARGET_GROUP })) };
    REQUIRE(members.has_value());
    REQUIRE(!members->empty());
    CHECK(!members->front().raw["request"].isMember("no_cache"));
}

auto main(int argc, char** argv) -> int {
    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    drogon::app().loadConfigFile("config.json");
    serve_fake_napcat();
    return test_support::run_tests_with_app(argc, argv);
}
