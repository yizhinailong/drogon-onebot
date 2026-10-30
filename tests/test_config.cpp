/**
 * @file test_config.cpp
 * @brief Check how `OneBotClient::initAndStart` reads and validates the plugin config.
 *
 * Calls `initAndStart` on plugins built by the test, without running the app.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

#include <drogon/drogon_test.h>
#include <trantor/utils/Logger.h>

#include <drogon_onebot/onebot_client.hpp>

#include "support.hpp"

namespace {

    constexpr char const* TOKEN_ENV{ "DROGON_ONEBOT_TEST_TOKEN" }; ///< Variable that the access token tests set.
    constexpr char const* UNSET_ENV{ "DROGON_ONEBOT_TEST_UNSET" }; ///< Variable that no test sets.

    /**
     * @brief Collect what trantor logs while this object lives, instead of printing it.
     *
     * trantor writes each line synchronously on the logging thread, here the test thread.
     */
    class LogCapture {
    private:
        std::string m_text; ///< Every line logged since construction.

    public:
        /**
         * @brief Send trantor's log output into this object.
         */
        LogCapture() {
            trantor::Logger::setOutputFunction([this](char const* msg, std::uint64_t len) { m_text.append(msg, static_cast<std::size_t>(len)); }, [] {});
        }

        /**
         * @brief Send trantor's log output to standard output again, as by default.
         */
        ~LogCapture() {
            trantor::Logger::setOutputFunction([](char const* msg, std::uint64_t len) { std::fwrite(msg, 1, static_cast<std::size_t>(len), stdout); }, [] { std::fflush(stdout); });
        }

        LogCapture(LogCapture const&) = delete;
        auto operator=(LogCapture const&) -> LogCapture& = delete;

        /**
         * @brief Return what was logged.
         * @return Every line logged since construction.
         */
        [[nodiscard]] auto Text() const -> std::string const& {
            return m_text;
        }
    };

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

    /**
     * @brief Initialize a new plugin with `config` and collect what it logs.
     * @param config Valid plugin config to pass to `initAndStart`.
     * @return The log lines of `initAndStart`.
     */
    auto init_log(Json::Value const& config) -> std::string {
        LogCapture log{};
        drogon_onebot::OneBotClient plugin{};
        plugin.initAndStart(config);
        return log.Text();
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

DROGON_TEST(WarnsAboutUnsignedEventRoute) {
    auto config{ minimal_config() };
    CHECK(!init_log(config).contains("unsigned"));

    config["event_path"] = test_support::EVENT_PATH;
    CHECK(init_log(config).contains("event_path /onebot/event accepts unsigned events"));

    test_support::set_env(test_support::SECRET_ENV, test_support::SECRET);
    config["secret_env"] = test_support::SECRET_ENV;
    CHECK(!init_log(config).contains("unsigned"));
    test_support::unset_env(test_support::SECRET_ENV);
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
