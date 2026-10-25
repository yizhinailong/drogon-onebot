/**
 * @file onebot_client.hpp
 * @brief Declare the `OneBot` client and the drogon plugin that holds its endpoint settings.
 */
#pragma once

#include <memory>
#include <string>

#include <drogon/plugins/Plugin.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include "drogon_onebot/error.hpp"
#include "drogon_onebot/types/action.hpp"

namespace drogon_onebot {

    namespace details {

        class Transport;

    } // namespace details

    /**
     * @brief Endpoint settings of a `OneBot`; the plugin reads them from its `config` object.
     */
    struct OneBotConfig {
        std::string base_url;     ///< HTTP API base URL of the OneBot v11 implementation, e.g. `http://127.0.0.1:3000`; may carry a path prefix.
        std::string access_token; ///< Access token from the `access_token_env` variable; empty without it.
    };

    /**
     * @brief Client for the HTTP API of one OneBot v11 implementation.
     *
     * @code
     * auto const info{ co_await bot.CallAction({ .action = "get_login_info" }) };
     * if (info) {
     *     std::println("user_id: {}", (*info)["user_id"].asInt64());
     * }
     * @endcode
     *
     * A cheap handle: copies share one transport, which is safe to use from any thread.
     * Pass it to coroutines by value.
     */
    class OneBot final {
    private:
        std::shared_ptr<details::Transport> m_transport; ///< Connection shared by all copies.

    public:
        /**
         * @brief Create a client for the endpoint in `config`.
         * @param config Endpoint settings; `access_token` may be empty for endpoints without auth.
         */
        explicit OneBot(OneBotConfig config);

        /**
         * @brief Call any action and await its `data` field.
         * @param params Action name and parameters, taken by value so they outlive the caller's frame.
         * @return Task yielding the response envelope's `data` (null for an `async` response), or why the action failed.
         * @note The request runs on the calling thread's event loop (the main loop when the
         * thread runs none), so the coroutine resumes on the thread that awaited it.
         */
        [[nodiscard]] auto CallAction(
            CallActionParams params
        ) const -> drogon::Task<Result<Json::Value>>;
    };

    /**
     * @brief Drogon plugin that owns the OneBot endpoint settings.
     *
     * Register it in the application's config.json:
     * @code{.json}
     * "plugins": [{
     *     "name": "drogon_onebot::OneBotClient",
     *     "config": { "base_url": "http://127.0.0.1:3000", "access_token_env": "ONEBOT_ACCESS_TOKEN" }
     * }]
     * @endcode
     *
     * The access token is never read from config.json: `access_token_env` names
     * the environment variable that holds it. Omit `access_token_env` when the
     * OneBot implementation has no access token.
     *
     * Then fetch it once `app().run()` has started, e.g. from a beginning advice
     * or a handler: `drogon::app().getPlugin<drogon_onebot::OneBotClient>()`, and
     * call `NewOneBotClient()` for a `OneBot` bound to these settings.
     *
     * @note With a static build, the plugin registers itself only if the
     * application references this class (calling `getPlugin<OneBotClient>()` is
     * enough). Otherwise the linker drops it and drogon cannot find the name.
     */
    class OneBotClient : public drogon::Plugin<OneBotClient> {
    private:
        OneBotConfig m_config; ///< Settings parsed in `initAndStart()`.

    public:
        /**
         * @brief Read the endpoint settings from the plugin's `config` object.
         * @param config The plugin's `config` object from config.json.
         * @throws std::invalid_argument If `base_url` is missing or empty, or if
         * `access_token_env` names an unset or empty environment variable.
         */
        auto initAndStart(Json::Value const& config) -> void override;

        /**
         * @brief Release resources on application shutdown.
         */
        auto shutdown() -> void override;

        /**
         * @brief Return the settings parsed from config.json.
         * @return Settings valid after drogon has initialized the plugin.
         */
        [[nodiscard]] auto Config() const noexcept -> OneBotConfig const&;

        /**
         * @brief Create a `OneBot` client for the configured endpoint.
         * @return A new client; call only after drogon has initialized the plugin.
         */
        [[nodiscard]] auto NewOneBotClient() const -> OneBot;
    };

} // namespace drogon_onebot
