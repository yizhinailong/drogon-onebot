/**
 * @file onebot_client.hpp
 * @brief Declare the `OneBot` client and the drogon plugin that holds its settings and receives events.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <drogon/plugins/Plugin.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include "drogon_onebot/error.hpp"
#include "drogon_onebot/types/account.hpp"
#include "drogon_onebot/types/action.hpp"
#include "drogon_onebot/types/credential.hpp"
#include "drogon_onebot/types/event.hpp"
#include "drogon_onebot/types/file.hpp"
#include "drogon_onebot/types/group.hpp"
#include "drogon_onebot/types/message.hpp"
#include "drogon_onebot/types/request.hpp"
#include "drogon_onebot/types/system.hpp"

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
     *
     * Each method reads its parameters and hands the returned task its own share of the
     * transport before it returns. The task may therefore outlive this handle and the
     * parameters, even though it starts only when awaited.
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
         * @param params Action name and parameters, read before this returns.
         * @return Task yielding the response envelope's `data` (null for an `async` response), or why the action failed.
         * @note The request runs on the calling thread's event loop (the main loop when the
         * thread runs none), so the coroutine resumes on the thread that awaited it.
         */
        [[nodiscard]] auto CallAction(
            CallActionParams params
        ) const -> drogon::Task<Result<Json::Value>>;

        /**
         * @brief Call `get_login_info`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the logged-in account, or why the action failed.
         */
        [[nodiscard]] auto GetLoginInfo(
            GetLoginInfoParams params = {}
        ) const -> drogon::Task<Result<LoginInfo>>;

        /**
         * @brief Call `get_stranger_info`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the account's public profile, or why the action failed.
         */
        [[nodiscard]] auto GetStrangerInfo(
            GetStrangerInfoParams params
        ) const -> drogon::Task<Result<StrangerInfo>>;

        /**
         * @brief Call `send_like`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing once the likes are sent, or why the action failed.
         * @note NapCat reports a like that QQ refuses as `ErrorKind::Action`.
         */
        [[nodiscard]] auto SendLike(
            SendLikeParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `get_friend_list`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the bot account's friends, or why the action failed.
         */
        [[nodiscard]] auto GetFriendList(
            GetFriendListParams params = {}
        ) const -> drogon::Task<Result<std::vector<FriendInfo>>>;

        /**
         * @brief Call `send_private_msg`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the sent message's ID, or why the action failed.
         */
        [[nodiscard]] auto SendPrivateMsg(
            SendPrivateMsgParams params
        ) const -> drogon::Task<Result<SentMessage>>;

        /**
         * @brief Call `send_group_msg`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the sent message's ID, or why the action failed.
         */
        [[nodiscard]] auto SendGroupMsg(
            SendGroupMsgParams params
        ) const -> drogon::Task<Result<SentMessage>>;

        /**
         * @brief Call `send_msg`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the sent message's ID, or why the action failed.
         * @note Without `message_type`, NapCat sends to `group_id` when it is set and to
         * `user_id` otherwise. With `private`, it sends to a non-friend `user_id` as a
         * temporary chat through `group_id`.
         */
        [[nodiscard]] auto SendMsg(
            SendMsgParams params
        ) const -> drogon::Task<Result<SentMessage>>;

        /**
         * @brief Call `delete_msg`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing once the message is recalled, or why the action failed.
         * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
         * an older ID fails with `ErrorKind::Action`.
         */
        [[nodiscard]] auto DeleteMsg(
            DeleteMsgParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `get_msg`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the message, or why the action failed.
         * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
         * an older ID fails with `ErrorKind::Action`.
         */
        [[nodiscard]] auto GetMsg(
            GetMsgParams params
        ) const -> drogon::Task<Result<MessageInfo>>;

        /**
         * @brief Call `get_forward_msg`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the forwarded messages, or why the action failed.
         * @note NapCat answers with go-cqhttp's `messages`, whole messages shaped like those
         * of `get_msg`, instead of the standard's `node` segments; this reads NapCat's shape.
         * Like `GetMsg()`, it fails with `ErrorKind::Action` for a forward that NapCat has not
         * seen since it started.
         */
        [[nodiscard]] auto GetForwardMsg(
            GetForwardMsgParams params
        ) const -> drogon::Task<Result<ForwardMessage>>;

        /**
         * @brief Call `get_group_info`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the group, or why the action failed.
         */
        [[nodiscard]] auto GetGroupInfo(
            GetGroupInfoParams params
        ) const -> drogon::Task<Result<GroupInfo>>;

        /**
         * @brief Call `get_group_list`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the groups the bot account is in, or why the action failed.
         */
        [[nodiscard]] auto GetGroupList(
            GetGroupListParams params = {}
        ) const -> drogon::Task<Result<std::vector<GroupInfo>>>;

        /**
         * @brief Call `get_group_member_info`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the group member, or why the action failed.
         */
        [[nodiscard]] auto GetGroupMemberInfo(
            GetGroupMemberInfoParams params
        ) const -> drogon::Task<Result<GroupMemberInfo>>;

        /**
         * @brief Call `get_group_member_list`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the group's members, or why the action failed.
         */
        [[nodiscard]] auto GetGroupMemberList(
            GetGroupMemberListParams params
        ) const -> drogon::Task<Result<std::vector<GroupMemberInfo>>>;

        /**
         * @brief Call `get_group_honor_info`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the group's honors, or why the action failed.
         * @note NapCat reads the honors from QQ's web page and leaves a list empty when that
         * fails, so an empty list does not prove that nobody holds the honor.
         */
        [[nodiscard]] auto GetGroupHonorInfo(
            GetGroupHonorInfoParams params
        ) const -> drogon::Task<Result<GroupHonorInfo>>;

        /**
         * @brief Call `set_group_kick`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat does not check QQ's answer, so a removal that QQ refuses, e.g.
         * without admin rights, still succeeds.
         */
        [[nodiscard]] auto SetGroupKick(
            SetGroupKickParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_ban`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat fails with `ErrorKind::Action` for a member not in the group, for the
         * group owner, and for an admin unless the bot account owns the group.
         */
        [[nodiscard]] auto SetGroupBan(
            SetGroupBanParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_whole_ban`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat reports QQ's refusal, e.g. without admin rights, as `ErrorKind::Action`.
         */
        [[nodiscard]] auto SetGroupWholeBan(
            SetGroupWholeBanParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_admin`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat does not check QQ's answer, so a change that QQ refuses, e.g. when
         * the bot account does not own the group, still succeeds.
         */
        [[nodiscard]] auto SetGroupAdmin(
            SetGroupAdminParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_card`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat reads the card back for about 3 seconds after setting it, and fails
         * with `ErrorKind::Action` when it has not changed by then.
         */
        [[nodiscard]] auto SetGroupCard(
            SetGroupCardParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_name`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat reports QQ's refusal as `ErrorKind::Action`.
         */
        [[nodiscard]] auto SetGroupName(
            SetGroupNameParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_leave`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat does not check QQ's answer, so a request that QQ refuses still succeeds.
         */
        [[nodiscard]] auto SetGroupLeave(
            SetGroupLeaveParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_special_title`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat sets titles through its packet backend and fails with
         * `ErrorKind::Action` (retcode 400) when that is unavailable.
         */
        [[nodiscard]] auto SetGroupSpecialTitle(
            SetGroupSpecialTitleParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_friend_add_request`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat fails with `ErrorKind::Action` when no pending request has `flag`.
         * Otherwise it does not wait for QQ's answer, so a refused request still succeeds.
         */
        [[nodiscard]] auto SetFriendAddRequest(
            SetFriendAddRequestParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `set_group_add_request`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing on success, or why the action failed.
         * @note NapCat fails with `ErrorKind::Action` when it finds no request with `flag`
         * among the bot's invitations and its latest group notifications. Otherwise it does
         * not check QQ's answer, so a refused request still succeeds.
         */
        [[nodiscard]] auto SetGroupAddRequest(
            SetGroupAddRequestParams params
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `get_cookies`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the bot account's cookies, or why the action failed.
         * @note NapCat also answers with the cookies' CSRF token as the string `bkn`, which
         * `Cookies::raw` keeps.
         */
        [[nodiscard]] auto GetCookies(
            GetCookiesParams params = {}
        ) const -> drogon::Task<Result<Cookies>>;

        /**
         * @brief Call `get_csrf_token`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the bot account's CSRF token, or why the action failed.
         * @note NapCat fails with `ErrorKind::Action` when it cannot get the account's `skey` from QQ.
         */
        [[nodiscard]] auto GetCsrfToken(
            GetCsrfTokenParams params = {}
        ) const -> drogon::Task<Result<CsrfToken>>;

        /**
         * @brief Call `get_credentials`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the bot account's cookies and CSRF token, or why the action failed.
         * @note NapCat sends the token as `token`, like `get_csrf_token`, instead of the
         * standard's `csrf_token`; this reads either.
         */
        [[nodiscard]] auto GetCredentials(
            GetCredentialsParams params = {}
        ) const -> drogon::Task<Result<Credentials>>;

        /**
         * @brief Call `get_record`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the converted voice file, or why the action failed.
         * @note NapCat converts the file with ffmpeg next to the original and also answers with
         * its content as `base64`, which `FileInfo::raw` keeps. It finds `file` among the media
         * of the messages it has received, in memory for a day, or else by name through QQ's
         * file assistant, and fails with `ErrorKind::Action` when neither has it.
         */
        [[nodiscard]] auto GetRecord(
            GetRecordParams params
        ) const -> drogon::Task<Result<FileInfo>>;

        /**
         * @brief Call `get_image`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the downloaded image file, or why the action failed.
         * @note NapCat answers with the content as `base64` only when its `enableLocalFile2Url`
         * setting is on. It finds `file` as `GetRecord()` describes.
         */
        [[nodiscard]] auto GetImage(
            GetImageParams params
        ) const -> drogon::Task<Result<FileInfo>>;

        /**
         * @brief Call `can_send_image`.
         * @param params Request fields, read before this returns.
         * @return Task yielding whether the implementation can send images, or why the action failed.
         * @note NapCat always answers yes.
         */
        [[nodiscard]] auto CanSendImage(
            CanSendImageParams params = {}
        ) const -> drogon::Task<Result<CanSend>>;

        /**
         * @brief Call `can_send_record`.
         * @param params Request fields, read before this returns.
         * @return Task yielding whether the implementation can send voice, or why the action failed.
         * @note NapCat always answers yes.
         */
        [[nodiscard]] auto CanSendRecord(
            CanSendRecordParams params = {}
        ) const -> drogon::Task<Result<CanSend>>;

        /**
         * @brief Call `get_status`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the implementation's running status, or why the action failed.
         * @note NapCat always sends `good` as true, even while the account is offline, so check `online`.
         */
        [[nodiscard]] auto GetStatus(
            GetStatusParams params = {}
        ) const -> drogon::Task<Result<Status>>;

        /**
         * @brief Call `get_version_info`.
         * @param params Request fields, read before this returns.
         * @return Task yielding the implementation's name and version, or why the action failed.
         */
        [[nodiscard]] auto GetVersionInfo(
            GetVersionInfoParams params = {}
        ) const -> drogon::Task<Result<VersionInfo>>;

        /**
         * @brief Call `set_restart`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing once the implementation accepted the restart, or why the action failed.
         * @note NapCat answers before it restarts its worker process, so its HTTP API and event
         * posts pause until the new process is up. Running in a single process or as its framework
         * build, it cannot restart itself and fails with `ErrorKind::Action`.
         */
        [[nodiscard]] auto SetRestart(
            SetRestartParams params = {}
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief Call `clean_cache`.
         * @param params Request fields, read before this returns.
         * @return Task yielding nothing once the implementation cleaned its cache, or why the action failed.
         * @note NapCat deletes the files directly inside its temp directory and QQ's media and log
         * directories, not their subdirectories. It only logs the files it cannot delete, and fails
         * with `ErrorKind::Action` only when it cannot read its temp directory.
         */
        [[nodiscard]] auto CleanCache(
            CleanCacheParams params = {}
        ) const -> drogon::Task<Result<void>>;
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
     * Without `secret_env`, the route accepts unsigned events from anyone who can
     * reach the listener, and `initAndStart()` logs a warning about it.
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
         * handlers from IO threads without a lock, so once the listeners have
         * started, this logs an error and drops `handler`.
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
         * @brief Check that a handler may still be added, and log why it cannot or will never run.
         * @param method Name of the registering method, for the log message.
         * @return False once the listeners have started, when the handler must be dropped.
         */
        [[nodiscard]] auto acceptsHandler(char const* method) const -> bool;
    };

} // namespace drogon_onebot
