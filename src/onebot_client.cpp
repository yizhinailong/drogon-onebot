/**
 * @file onebot_client.cpp
 * @brief Implement the `OneBot` client and the drogon plugin that holds its settings and receives events.
 */
#include "drogon_onebot/onebot_client.hpp"

#include <cstddef>
#include <cstdlib>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <drogon/HttpAppFramework.h>
#include <trantor/utils/Logger.h>

#include "details/event_receiver.hpp"
#include "details/transport.hpp"

namespace {

    /**
     * @brief Read an environment variable of this process.
     *
     * Windows uses `_dupenv_s`, which returns a copy, because the UCRT deprecates `getenv`.
     *
     * @param name Variable name.
     * @return The variable's value, or an empty string when it is unset.
     */
    auto read_env(char const* name) -> std::string {
#ifdef _WIN32
        char* buffer{ nullptr };
        std::size_t size{ 0 };
        if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
            return {};
        }
        std::unique_ptr<char, decltype(&std::free)> const owner{ buffer, &std::free };
        return std::string{ buffer };
#else
        char const* value{ std::getenv(name) };
        return value != nullptr ? value : "";
#endif
    }

    /**
     * @brief Read a secret from the named environment variable.
     * @param env_name Variable name from `access_token_env` or `secret_env`; empty when there is no secret.
     * @return The variable's value, or an empty string when `env_name` is empty.
     * @throws std::invalid_argument If the variable is unset or empty.
     */
    auto read_secret_env(std::string const& env_name) -> std::string {
        if (env_name.empty()) {
            return {};
        }
        std::string value{ read_env(env_name.c_str()) };
        if (value.empty()) {
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
     * @brief Read the settings from the plugin's `config` object and register the event route.
     * @param config The plugin's `config` object from config.json.
     * @throws std::invalid_argument If `base_url` is missing or empty, if
     * `event_path` is set but does not start with `/`, or if `access_token_env`
     * or `secret_env` names an unset or empty environment variable.
     */
    auto OneBotClient::initAndStart(Json::Value const& config) -> void {
        m_config = OneBotConfig{
            .base_url = config.get("base_url", "").asString(),
            .access_token = read_secret_env(config.get("access_token_env", "").asString()),
            .event_path = config.get("event_path", "").asString(),
            .secret = read_secret_env(config.get("secret_env", "").asString()),
        };
        if (m_config.base_url.empty()) {
            throw std::invalid_argument{ "drogon_onebot::OneBotClient: config needs a non-empty base_url" };
        }
        if (!m_config.event_path.empty() && !m_config.event_path.starts_with('/')) {
            throw std::invalid_argument{ "drogon_onebot::OneBotClient: event_path must start with /" };
        }

        m_receiver = std::make_shared<details::EventReceiver>(m_config.secret);
        if (!m_config.event_path.empty()) {
            // Plugins are initialized before the routers, so the route can still be added
            // here, as drogon's own PromExporter does (HttpAppFrameworkImpl.cc:663-671).
            drogon::app().registerHandler(
                m_config.event_path,
                [receiver = m_receiver](
                    drogon::HttpRequestPtr const& req,
                    std::function<void(drogon::HttpResponsePtr const&)>&& callback
                ) {
                    receiver->Receive(req, std::move(callback));
                },
                { drogon::Post },
                "drogon_onebot::OneBotClient events"
            );
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

    /**
     * @brief Run `handler` for every message event posted to `event_path`.
     * @param handler Coroutine to run. Each event runs a copy of it, so its
     * captures stay alive until that run completes. An exception it throws is
     * logged and dropped.
     * @note Call only after drogon has initialized the plugin, and before the
     * listeners start: from a beginning advice, or from `initAndStart()` of a
     * plugin that lists this one under `dependencies`. The route reads the
     * handlers from IO threads without a lock.
     */
    auto OneBotClient::OnMessage(MessageHandler handler) -> void {
        warnWithoutRoute("OnMessage");
        m_receiver->AddMessageHandler(std::move(handler));
    }

    /**
     * @brief Run `handler` for every notice event posted to `event_path`.
     * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
     * @note Register handlers at the times that `OnMessage()` lists.
     */
    auto OneBotClient::OnNotice(NoticeHandler handler) -> void {
        warnWithoutRoute("OnNotice");
        m_receiver->AddNoticeHandler(std::move(handler));
    }

    /**
     * @brief Run `handler` for every request event posted to `event_path`.
     * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
     * @note Register handlers at the times that `OnMessage()` lists.
     */
    auto OneBotClient::OnRequest(RequestHandler handler) -> void {
        warnWithoutRoute("OnRequest");
        m_receiver->AddRequestHandler(std::move(handler));
    }

    /**
     * @brief Run `handler` for every meta event posted to `event_path`.
     * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
     * @note Register handlers at the times that `OnMessage()` lists.
     */
    auto OneBotClient::OnMetaEvent(MetaEventHandler handler) -> void {
        warnWithoutRoute("OnMetaEvent");
        m_receiver->AddMetaEventHandler(std::move(handler));
    }

    /**
     * @brief Log that a handler will never run because no event route is registered.
     * @param method Name of the registering method, for the log message.
     */
    auto OneBotClient::warnWithoutRoute(char const* method) const -> void {
        if (m_config.event_path.empty()) {
            LOG_WARN << "drogon_onebot::OneBotClient: " << method << " has no effect without event_path in the plugin config";
        }
    }

} // namespace drogon_onebot
