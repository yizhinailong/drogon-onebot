/**
 * @file onebot_client.hpp
 * @brief Declare the `OneBot` client and the drogon plugin that holds its settings and receives events.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

#include <drogon/plugins/Plugin.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include "drogon_onebot/error.hpp"
#include "drogon_onebot/types/account.hpp"
#include "drogon_onebot/types/action.hpp"
#include "drogon_onebot/types/event.hpp"
#include "drogon_onebot/types/group.hpp"
#include "drogon_onebot/types/message.hpp"

namespace drogon_onebot {

    namespace details {

        class EventReceiver;
        class Transport;

    } // namespace details

    /**
     * @brief Settings of the plugin: the HTTP API endpoint of a `OneBot` and the event route.
     *
     * The plugin reads them from its `config` object.
     */
    struct OneBotConfig {
        std::string base_url;     ///< HTTP API base URL of the OneBot v11 implementation, e.g. `http://127.0.0.1:3000`; may carry a path prefix.
        std::string access_token; ///< Access token from the `access_token_env` variable; empty without it.
        std::string event_path;   ///< Path of the route that receives posted events, e.g. `/onebot/event`; empty registers no route.
        std::string secret;       ///< Secret from the `secret_env` variable that signs posted events; empty accepts unsigned events.
    };

    /**
     * @brief Coroutine that handles a message event.
     */
    using MessageHandler = std::function<drogon::Task<>(MessageEvent)>;

    /**
     * @brief Coroutine that handles a notice event.
     */
    using NoticeHandler = std::function<drogon::Task<>(NoticeEvent)>;

    /**
     * @brief Coroutine that handles a request event.
     */
    using RequestHandler = std::function<drogon::Task<>(RequestEvent)>;

    /**
     * @brief Coroutine that handles a meta event.
     */
    using MetaEventHandler = std::function<drogon::Task<>(MetaEvent)>;

    /**
     * @brief Client for the HTTP API of one OneBot v11 implementation.
     *
     * Methods mirror OneBot v11 action names: `SendPrivateMsg()` calls `send_private_msg`.
     *
     * @code
     * auto const sent{ co_await bot.SendGroupMsg({
     *     .group_id = 123456,
     *     .message = { drogon_onebot::MessageSegment::Text("Hello!") },
     * }) };
     * if (sent) {
     *     std::println("message_id: {}", sent->message_id);
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

        /**
         * @brief Call `get_login_info`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding the logged-in account, or why the action failed.
         */
        [[nodiscard]] auto GetLoginInfo(
            GetLoginInfoParams params = {}
        ) const -> drogon::Task<Result<LoginInfo>>;

        /**
         * @brief Call `get_stranger_info`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding the account's public profile, or why the action failed.
         */
        [[nodiscard]] auto GetStrangerInfo(
            GetStrangerInfoParams params
        ) const -> drogon::Task<Result<StrangerInfo>>;

        /**
         * @brief Call `send_private_msg`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding the sent message's ID, or why the action failed.
         */
        [[nodiscard]] auto SendPrivateMsg(
            SendPrivateMsgParams params
        ) const -> drogon::Task<Result<SentMessage>>;

        /**
         * @brief Call `send_group_msg`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding the sent message's ID, or why the action failed.
         */
        [[nodiscard]] auto SendGroupMsg(
            SendGroupMsgParams params
        ) const -> drogon::Task<Result<SentMessage>>;

        /**
         * @brief Call `delete_msg`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding nothing once the message is recalled, or why the action failed.
         * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
         * an older ID fails with `ErrorKind::Action`.
         */
        [[nodiscard]] auto DeleteMsg(
            DeleteMsgParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `get_msg`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding the message, or why the action failed.
         * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
         * an older ID fails with `ErrorKind::Action`.
         */
        [[nodiscard]] auto GetMsg(
            GetMsgParams params
        ) const -> drogon::Task<Result<MessageInfo>>;

        /**
         * @brief Call `get_group_info`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding the group, or why the action failed.
         */
        [[nodiscard]] auto GetGroupInfo(
            GetGroupInfoParams params
        ) const -> drogon::Task<Result<GroupInfo>>;

        /**
         * @brief Call `get_group_member_info`.
         * @param params Request fields, taken by value so they outlive the caller's frame.
         * @return Task yielding the group member, or why the action failed.
         */
        [[nodiscard]] auto GetGroupMemberInfo(
            GetGroupMemberInfoParams params
        ) const -> drogon::Task<Result<GroupMemberInfo>>;
    };

    /**
     * @brief Drogon plugin that owns the OneBot settings and receives the events posted to it.
     *
     * Register it in the application's config.json:
     * @code{.json}
     * "plugins": [{
     *     "name": "drogon_onebot::OneBotClient",
     *     "config": {
     *         "base_url": "http://127.0.0.1:3000",
     *         "access_token_env": "ONEBOT_ACCESS_TOKEN",
     *         "event_path": "/onebot/event",
     *         "secret_env": "ONEBOT_SECRET"
     *     }
     * }]
     * @endcode
     *
     * Secrets are never read from config.json: `access_token_env` and `secret_env`
     * name the environment variables that hold the access token of the HTTP API
     * and the secret that signs posted events. Omit either when the OneBot
     * implementation has none.
     *
     * With `event_path` set, the plugin registers a `POST` route there for the
     * implementation's HTTP POST reporting (NapCat's HTTP client). The route
     * checks `X-Signature` when a secret is set, answers 204 at once, and then
     * runs the matching handlers in the background, so a slow handler does not
     * hold the implementation's request open. Reply through `NewOneBotClient()`.
     *
     * Fetch the plugin once `app().run()` has started, e.g. from a beginning advice
     * or a handler: `drogon::app().getPlugin<drogon_onebot::OneBotClient>()`.
     *
     * @note With a static build, the plugin registers itself only if the
     * application references this class (calling `getPlugin<OneBotClient>()` is
     * enough). Otherwise the linker drops it and drogon cannot find the name.
     */
    class OneBotClient : public drogon::Plugin<OneBotClient> {
    private:
        OneBotConfig m_config;                              ///< Settings parsed in `initAndStart()`.
        std::shared_ptr<details::EventReceiver> m_receiver; ///< Handlers behind the `event_path` route, shared with it; created in `initAndStart()`.

    public:
        /**
         * @brief Read the settings from the plugin's `config` object and register the event route.
         * @param config The plugin's `config` object from config.json.
         * @throws std::invalid_argument If `base_url` is missing or empty, if
         * `event_path` is set but does not start with `/`, or if `access_token_env`
         * or `secret_env` names an unset or empty environment variable.
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
        auto OnMessage(MessageHandler handler) -> void;

        /**
         * @brief Run `handler` for every notice event posted to `event_path`.
         * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
         * @note Register handlers at the times that `OnMessage()` lists.
         */
        auto OnNotice(NoticeHandler handler) -> void;

        /**
         * @brief Run `handler` for every request event posted to `event_path`.
         * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
         * @note Register handlers at the times that `OnMessage()` lists.
         */
        auto OnRequest(RequestHandler handler) -> void;

        /**
         * @brief Run `handler` for every meta event posted to `event_path`.
         * @param handler Coroutine to run; see `OnMessage()` for its lifetime and exceptions.
         * @note Register handlers at the times that `OnMessage()` lists.
         */
        auto OnMetaEvent(MetaEventHandler handler) -> void;

    private:
        /**
         * @brief Log that a handler will never run because no event route is registered.
         * @param method Name of the registering method, for the log message.
         */
        auto warnWithoutRoute(char const* method) const -> void;
    };

} // namespace drogon_onebot
