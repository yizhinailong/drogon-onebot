/**
 * @file test_config.cpp
 * @brief Check how `OneBotClient::initAndStart` reads and validates the plugin config.
 *
 * Calls `initAndStart` on plugins built by the test, without running the app.
 */
#include <stdexcept>
#include <string>

#include <drogon/drogon_test.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {

    constexpr char const* TOKEN_ENV{ "DROGON_ONEBOT_TEST_TOKEN" }; ///< Variable that the access token tests set.
    constexpr char const* UNSET_ENV{ "DROGON_ONEBOT_TEST_UNSET" }; ///< Variable that no test sets.

    /**
     * @brief Build a plugin config that has only `base_url`.
     * @return Config object with `base_url` `http://127.0.0.1:3000`.
     */
    auto minimal_config() -> Json::Value {
        Json::Value config{ Json::objectValue };
        config["base_url"] = "http://127.0.0.1:3000";
        return config;
    }

    /**
     * @brief Initialize a new plugin with `config` and report how it was rejected.
     * @param config Plugin config to pass to `initAndStart`.
     * @return The `std::invalid_argument` message, or `(accepted)` if nothing was thrown.
     */
    auto rejection(Json::Value const& config) -> std::string {
        drogon_onebot::OneBotClient plugin{};
        try {
            plugin.initAndStart(config);
        } catch (std::invalid_argument const& e) {
            return e.what();
        }
        return "(accepted)";
    }

} // namespace

DROGON_TEST(RequiresBaseUrl) {
    CHECK(rejection(Json::Value{ Json::objectValue }).contains("non-empty base_url"));

    auto config{ minimal_config() };
    config["base_url"] = "";
    CHECK(rejection(config).contains("non-empty base_url"));
}

DROGON_TEST(ReadsMinimalConfig) {
    drogon_onebot::OneBotClient plugin{};
    plugin.initAndStart(minimal_config());
    auto const& config{ plugin.Config() };
    CHECK(config.base_url == "http://127.0.0.1:3000");
    CHECK(config.access_token.empty());
    CHECK(config.event_path.empty());
    CHECK(config.secret.empty());
}

DROGON_TEST(ReadsAccessTokenFromEnv) {
    test_support::set_env(TOKEN_ENV, "token-value");
    auto config_json{ minimal_config() };
    config_json["access_token_env"] = TOKEN_ENV;
    drogon_onebot::OneBotClient plugin{};
    plugin.initAndStart(config_json);
    CHECK(plugin.Config().access_token == "token-value");
    test_support::unset_env(TOKEN_ENV);
}

DROGON_TEST(RejectsUnsetAccessTokenEnv) {
    auto config{ minimal_config() };
    config["access_token_env"] = UNSET_ENV;
    CHECK(rejection(config).contains("DROGON_ONEBOT_TEST_UNSET is unset or empty"));
}

DROGON_TEST(ReadsSecretFromEnv) {
    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    auto config_json{ minimal_config() };
    config_json["secret_env"] = test_support::SECRET_ENV;
    drogon_onebot::OneBotClient plugin{};
    plugin.initAndStart(config_json);
    CHECK(plugin.Config().secret == test_support::SECRET);
    test_support::unset_env(test_support::SECRET_ENV);
}

DROGON_TEST(RejectsUnsetSecretEnv) {
    auto config{ minimal_config() };
    config["secret_env"] = UNSET_ENV;
    CHECK(rejection(config).contains("DROGON_ONEBOT_TEST_UNSET is unset or empty"));
}

DROGON_TEST(RejectsRelativeEventPath) {
    auto config{ minimal_config() };
    config["event_path"] = "onebot/event";
    CHECK(rejection(config).contains("event_path must start with /"));
}

DROGON_TEST(AcceptsTrackedConfigJson) {
    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    auto const root{ test_support::read_config() };
    REQUIRE(root["plugins"][0]["name"].asString() == "drogon_onebot::OneBotClient");

    drogon_onebot::OneBotClient plugin{};
    plugin.initAndStart(root["plugins"][0]["config"]);
    CHECK(plugin.Config().base_url == test_support::SERVER_URL);
    CHECK(plugin.Config().event_path == test_support::EVENT_PATH);
    CHECK(plugin.Config().secret == test_support::SECRET);
    test_support::unset_env(test_support::SECRET_ENV);
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
