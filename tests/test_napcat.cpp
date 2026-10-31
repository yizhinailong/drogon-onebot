/**
 * @file test_napcat.cpp
 * @brief Verify typed favourite extension payloads and NapCat response validation.
 */
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {
    test_support::Inbox<Json::Value> requests{}; ///< Payloads observed by the fake extensions.

    /**
     * @brief Construct a client for the test listener or a malformed response prefix.
     * @param prefix Optional fake API prefix.
     * @return An independent HTTP client.
     */
    auto client(std::string const& prefix = "") -> drogon_onebot::OneBot {
        return drogon_onebot::OneBot{ { .base_url = std::string{ test_support::SERVER_URL } + prefix } };
    }
} // namespace

DROGON_TEST(FavouritesUseTypedPayloadsAndPreserveRawMetadata) {
    auto const list{ drogon::sync_wait(client().FetchCustomFaceDetail({ .count = 5 })) };
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 1);
    CHECK(list->front().emoji_id == "7");
    CHECK(list->front().res_id == "resource");
    CHECK(list->front().description == "hello");
    CHECK(list->front().raw["extra"].asBool());
    auto body{ requests.Pop(test_support::EVENT_WAIT) };
    REQUIRE(body.has_value());
    CHECK((*body)["count"].asInt() == 5);

    REQUIRE(drogon::sync_wait(client().AddCustomFace({ .file = "/napcat/face.png", .is_origin = false })).has_value());
    body = requests.Pop(test_support::EVENT_WAIT);
    REQUIRE(body.has_value());
    CHECK((*body)["file"].asString() == "/napcat/face.png");
    CHECK(!(*body)["is_origin"].asBool());
    CHECK(!body->isMember("emoji_id"));

    REQUIRE(drogon::sync_wait(client().SetCustomFaceDesc({ .emoji_id = "7", .res_id = "resource", .md5 = "abc", .description = "new" })).has_value());
    body = requests.Pop(test_support::EVENT_WAIT);
    REQUIRE(body.has_value());
    CHECK((*body)["emoji_id"].asString() == "7");
    CHECK((*body)["desc"].asString() == "new");

    REQUIRE(drogon::sync_wait(client().DeleteCustomFace({ .res_ids = { "a", "b" } })).has_value());
    body = requests.Pop(test_support::EVENT_WAIT);
    REQUIRE(body.has_value());
    CHECK((*body)["ids"].size() == 2);
    CHECK((*body)["ids"][1].asString() == "b");
}

DROGON_TEST(FavouritesRejectMalformedEntriesAndPropagateActionFailures) {
    auto const invalid{ drogon::sync_wait(client("/invalid").FetchCustomFaceDetail()) };
    REQUIRE(!invalid.has_value());
    CHECK(invalid.error().kind == drogon_onebot::ErrorKind::ResponseValidation);
    auto const failed{ drogon::sync_wait(client("/failed").DeleteCustomFace({ .res_ids = { "x" } })) };
    REQUIRE(!failed.has_value());
    CHECK(failed.error().kind == drogon_onebot::ErrorKind::Action);
}

auto main(int argc, char** argv) -> int {
    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    drogon::app().loadConfigFile("config.json");
    for (auto const* action : { "fetch_custom_face_detail", "add_custom_face", "delete_custom_face", "set_custom_face_desc" }) {
        drogon::app().registerHandler(std::string{ "/" } + action, [action](drogon::HttpRequestPtr const& req, std::function<void(drogon::HttpResponsePtr const&)>&& callback) {
            requests.Push(*req->getJsonObject());
            Json::Value result{};
            result["status"] = "ok";
            result["retcode"] = 0;
            result["data"] = Json::Value{};
            if (std::string_view{ action } == "fetch_custom_face_detail") {
                Json::Value face{};
                face["emoId"] = 7;
                face["resId"] = "resource";
                face["md5"] = "abc";
                face["url"] = "https://example.test/face.png";
                face["desc"] = "hello";
                face["extra"] = true;
                result["data"] = Json::Value{ Json::arrayValue };
                result["data"].append(face);
            }
            callback(drogon::HttpResponse::newHttpJsonResponse(result));
        },
                                      { drogon::Post });
    }
    drogon::app().registerHandler("/invalid/fetch_custom_face_detail", [](drogon::HttpRequestPtr const&, std::function<void(drogon::HttpResponsePtr const&)>&& callback) {
        Json::Value result{};
        result["status"] = "ok";
        result["retcode"] = 0;
        result["data"] = Json::Value{ Json::arrayValue };
        result["data"].append("invalid");
        callback(drogon::HttpResponse::newHttpJsonResponse(result));
    },
                                  { drogon::Post });
    drogon::app().registerHandler("/failed/delete_custom_face", [](drogon::HttpRequestPtr const&, std::function<void(drogon::HttpResponsePtr const&)>&& callback) {
        Json::Value result{};
        result["status"] = "failed";
        result["retcode"] = 100;
        result["message"] = "not supported";
        callback(drogon::HttpResponse::newHttpJsonResponse(result));
    },
                                  { drogon::Post });
    return test_support::run_tests_with_app(argc, argv);
}
