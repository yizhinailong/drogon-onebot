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
#include <trantor/net/EventLoop.h>
#include <trantor/utils/Logger.h>

#include "details/action_call.hpp"
#include "details/event_receiver.hpp"
#include "details/http_transport.hpp"
#include "details/reverse_ws.hpp"
#include "details/reverse_ws_route.hpp"

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
     * @brief Create a client for the HTTP API in `config`.
     * @param config Endpoint settings; `access_token` may be empty for endpoints without auth.
     */
    OneBot::OneBot(OneBotConfig config)
        : m_transport{ std::make_shared<details::HttpTransport>(std::move(config)) } {}

    /**
     * @brief Create a client that calls actions through `transport`.
     * @param transport Transport to share, e.g. the plugin's reverse WebSocket.
     */
    OneBot::OneBot(std::shared_ptr<details::Transport> transport)
        : m_transport{ std::move(transport) } {}

    /**
     * @brief Call any action and await its `data` field.
     * @param params Action name and parameters, read before this returns.
     * @return Task yielding the response envelope's `data` (null for an `async` response), or why the action failed.
     * @note The coroutine resumes on the calling thread's event loop (the main loop when the
     * thread runs none). Over HTTP, the request also runs there.
     */
    auto OneBot::CallAction(
        CallActionParams params
    ) const -> drogon::Task<Result<Json::Value>> {
        return details::call(m_transport, std::move(params.action), std::move(params.params), params.timeout);
    }

    /**
     * @brief Read the settings from the plugin's `config` object and register the event routes.
     * @param config The plugin's `config` object from config.json.
     * @throws std::invalid_argument If both `base_url` and `ws_path` are missing
     * or empty, if `event_path` or `ws_path` is set but does not start with `/`,
     * or if `access_token_env` or `secret_env` names an unset or empty environment
     * variable.
     */
    auto OneBotClient::initAndStart(Json::Value const& config) -> void {
        m_config = OneBotConfig{
            .base_url = config.get("base_url", "").asString(),
            .access_token = read_secret_env(config.get("access_token_env", "").asString()),
            .event_path = config.get("event_path", "").asString(),
            .secret = read_secret_env(config.get("secret_env", "").asString()),
            .ws_path = config.get("ws_path", "").asString(),
        };
        if (m_config.base_url.empty() && m_config.ws_path.empty()) {
            throw std::invalid_argument{ "drogon_onebot::OneBotClient: config needs a non-empty base_url or ws_path" };
        }
        if (!m_config.event_path.empty() && !m_config.event_path.starts_with('/')) {
            throw std::invalid_argument{ "drogon_onebot::OneBotClient: event_path must start with /" };
        }
        if (!m_config.ws_path.empty() && !m_config.ws_path.starts_with('/')) {
            throw std::invalid_argument{ "drogon_onebot::OneBotClient: ws_path must start with /" };
        }

        m_receiver = std::make_shared<details::EventReceiver>(m_config.secret);
        // Beginning advices run in one main-loop task that then starts the listeners
        // (HttpAppFrameworkImpl.cc:674-682), so a task queued from an advice runs after every
        // advice, including those registered by plugins initialized after this one.
        drogon::app().registerBeginningAdvice([receiver = m_receiver] {
            drogon::app().getLoop()->queueInLoop([receiver] { receiver->CloseRegistration(); });
        });
        if (!m_config.event_path.empty()) {
            if (m_config.secret.empty()) {
                LOG_WARN << "drogon_onebot::OneBotClient: event_path " << m_config.event_path << " accepts unsigned events from anyone who can reach it; set secret_env to check X-Signature";
            }
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
        if (!m_config.ws_path.empty()) {
            if (m_config.access_token.empty()) {
                LOG_WARN << "drogon_onebot::OneBotClient: ws_path " << m_config.ws_path << " accepts connections from anyone who can reach it; set access_token_env to check Authorization";
            }
            m_reverse_ws = std::make_shared<details::ReverseWebSocket>(m_config.ws_path, m_receiver);
            // A WebSocket route, like the event route, can be added until the routers are
            // initialized (HttpAppFrameworkImpl.cc:285-291). drogon then looks the controller
            // and the middleware up by class name (HttpControllersRouter.cc:275-287,
            // MiddlewaresFunction.cc:150-158) and finds the instances registered here.
            drogon::app()
                .registerController(std::make_shared<details::ReverseWsController>(m_reverse_ws))
                .registerMiddleware(std::make_shared<details::ReverseWsHandshake>(m_config.access_token))
                .registerWebSocketController(
                    m_config.ws_path,
                    details::ReverseWsController::classTypeName(),
                    { drogon::Get, details::ReverseWsHandshake::classTypeName() }
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
     * @return A new client for the HTTP API at `base_url`, or for the reverse
     * WebSocket without one; call only after drogon has initialized the plugin.
     */
    auto OneBotClient::NewOneBotClient() const -> OneBot {
        if (m_config.base_url.empty()) {
            return OneBot{ m_reverse_ws };
        }
        return OneBot{ m_config };
    }

    /**
     * @brief Run `handler` for every message event that arrives on `event_path` or `ws_path`.
     * @param handler Coroutine to run. Each event runs a copy of it, so its
     * captures stay alive until that run completes. An exception it throws is
     * logged and dropped.
     * @note Call only after drogon has initialized the plugin, and before the
     * listeners start: from a beginning advice, or from `initAndStart()` of a
     * plugin that lists this one under `dependencies`. The routes read the
     * handlers from IO threads without a lock, so once the listeners have
     * started, this logs an error and drops `handler`.
     */
    auto OneBotClient::OnMessage(MessageHandler handler) -> void {
        if (acceptsHandler("OnMessage")) {
            m_receiver->AddMessageHandler(std::move(handler));
        }
    }

    /**
     * @brief Run `handler` for every notice event that arrives on `event_path` or `ws_path`.
     * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
     * @note Register handlers at the times that `OnMessage()` lists.
     */
    auto OneBotClient::OnNotice(NoticeHandler handler) -> void {
        if (acceptsHandler("OnNotice")) {
            m_receiver->AddNoticeHandler(std::move(handler));
        }
    }

    /**
     * @brief Run `handler` for every request event that arrives on `event_path` or `ws_path`.
     * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
     * @note Register handlers at the times that `OnMessage()` lists.
     */
    auto OneBotClient::OnRequest(RequestHandler handler) -> void {
        if (acceptsHandler("OnRequest")) {
            m_receiver->AddRequestHandler(std::move(handler));
        }
    }

    /**
     * @brief Run `handler` for every meta event that arrives on `event_path` or `ws_path`.
     * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
     * @note Register handlers at the times that `OnMessage()` lists. NapCat sends
     * meta events, its lifecycle `connect` and heartbeats, only over WebSocket.
     */
    auto OneBotClient::OnMetaEvent(MetaEventHandler handler) -> void {
        if (acceptsHandler("OnMetaEvent")) {
            m_receiver->AddMetaEventHandler(std::move(handler));
        }
    }

    /**
     * @brief Check that a handler may still be added, and log why it cannot or will never run.
     * @param method Name of the registering method, for the log message.
     * @return False once the listeners have started, when the handler must be dropped.
     */
    auto OneBotClient::acceptsHandler(char const* method) const -> bool {
        if (m_receiver->RegistrationClosed()) {
            LOG_ERROR << "drogon_onebot::OneBotClient: " << method << " dropped a handler added after the listeners started; add handlers from a beginning advice";
            return false;
        }
        if (m_config.event_path.empty() && m_config.ws_path.empty()) {
            LOG_WARN << "drogon_onebot::OneBotClient: " << method << " has no effect without event_path or ws_path in the plugin config";
        }
        return true;
    }

} // namespace drogon_onebot
