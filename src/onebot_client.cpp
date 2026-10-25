/**
 * @file onebot_client.cpp
 * @brief Implement the `OneBot` client and the drogon plugin that holds its endpoint settings.
 */
#include "drogon_onebot/onebot_client.hpp"

#include <cstdlib>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

#include "details/transport.hpp"

namespace {

    /**
     * @brief Read the access token from the named environment variable.
     * @param env_name Variable name from `access_token_env`; empty when the endpoint needs no token.
     * @return The variable's value, or an empty string when `env_name` is empty.
     * @throws std::invalid_argument If the variable is unset or empty.
     */
    auto read_access_token(std::string const& env_name) -> std::string {
        if (env_name.empty()) {
            return {};
        }
        char const* value{ std::getenv(env_name.c_str()) };
        if (value == nullptr || *value == '\0') {
            throw std::invalid_argument{ std::format("drogon_onebot::OneBotClient: environment variable {} is unset or empty", env_name) };
        }
        return value;
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Create a client for the endpoint in `config`.
     * @param config Endpoint settings; `access_token` may be empty for endpoints without auth.
     */
    OneBot::OneBot(OneBotConfig config)
        : m_transport{ std::make_shared<details::Transport>(std::move(config)) } {}

    /**
     * @brief Call any action and await its `data` field.
     * @param params Action name and parameters, taken by value so they outlive the caller's frame.
     * @return Task yielding the response envelope's `data` (null for an `async` response), or why the action failed.
     * @note The request runs on the calling thread's event loop (the main loop when the
     * thread runs none), so the coroutine resumes on the thread that awaited it.
     */
    auto OneBot::CallAction(
        CallActionParams params
    ) const -> drogon::Task<Result<Json::Value>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        co_return co_await transport->Call(std::move(params.action), std::move(params.params), params.timeout);
    }

    /**
     * @brief Read the endpoint settings from the plugin's `config` object.
     * @param config The plugin's `config` object from config.json.
     * @throws std::invalid_argument If `base_url` is missing or empty, or if
     * `access_token_env` names an unset or empty environment variable.
     */
    auto OneBotClient::initAndStart(Json::Value const& config) -> void {
        m_config = OneBotConfig{
            .base_url = config.get("base_url", "").asString(),
            .access_token = read_access_token(config.get("access_token_env", "").asString()),
        };
        if (m_config.base_url.empty()) {
            throw std::invalid_argument{ "drogon_onebot::OneBotClient: config needs a non-empty base_url" };
        }
    }

    /**
     * @brief Release resources on application shutdown.
     */
    auto OneBotClient::shutdown() -> void {}

    /**
     * @brief Return the settings parsed from config.json.
     * @return Settings valid after drogon has initialized the plugin.
     */
    auto OneBotClient::Config() const noexcept -> OneBotConfig const& {
        return m_config;
    }

    /**
     * @brief Create a `OneBot` client for the configured endpoint.
     * @return A new client; call only after drogon has initialized the plugin.
     */
    auto OneBotClient::NewOneBotClient() const -> OneBot {
        return OneBot{ m_config };
    }

} // namespace drogon_onebot
