/**
 * @file event.hpp
 * @brief Declare the event types that the OneBot implementation posts to the event route.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <json/value.h>

#include "drogon_onebot/types/message.hpp"

namespace drogon_onebot {

    /**
     * @brief Event with `post_type` `message`: a private or group message the bot received.
     */
    struct MessageEvent {
        std::int64_t time{};                  ///< Unix time in seconds when the event happened.
        std::int64_t self_id{};               ///< QQ number of the bot account that received the message.
        std::string message_type;             ///< `private` or `group`.
        std::string sub_type;                 ///< e.g. `friend` or `group` for private messages, `normal` or `anonymous` for group messages.
        std::int32_t message_id{};            ///< ID of the message, e.g. for `delete_msg`.
        std::int64_t user_id{};               ///< QQ number of the sender.
        std::optional<std::int64_t> group_id; ///< Group number of a group message; empty for private messages.
        Message message;                      ///< Message content in array format.
        std::string raw_message;              ///< Message content as a CQ-code string.
        std::int32_t font{};                  ///< Font ID.
        Sender sender;                        ///< Sender details.
        Json::Value raw;                      ///< Whole event as received, for fields without a member here.
    };

    /**
     * @brief File of a `group_upload` notice.
     */
    struct GroupUploadFile {
        std::string id;       ///< File ID.
        std::string name;     ///< File name.
        std::int64_t size{};  ///< Size in bytes.
        std::int64_t busid{}; ///< Business ID of the file; 0 when absent.
    };

    /**
     * @brief Fields of a `group_upload` notice: a member uploaded a group file.
     */
    struct GroupUploadNotice {
        std::int64_t group_id{}; ///< Group number.
        std::int64_t user_id{};  ///< QQ number of the member who uploaded the file.
        GroupUploadFile file;    ///< The uploaded file.
    };

    /**
     * @brief Fields of a `group_admin` notice: a member became or stopped being an admin, by `NoticeEvent::sub_type` `set` or `unset`.
     */
    struct GroupAdminNotice {
        std::int64_t group_id{}; ///< Group number.
        std::int64_t user_id{};  ///< QQ number of the member whose role changed.
    };

    /**
     * @brief Fields of a `group_decrease` notice: a member left or was removed, by `NoticeEvent::sub_type` `leave`, `kick`, `kick_me`, or NapCat's `disband`.
     */
    struct GroupDecreaseNotice {
        std::int64_t group_id{};    ///< Group number.
        std::int64_t operator_id{}; ///< QQ number of the member who removed them; NapCat sends 0 when it cannot tell.
        std::int64_t user_id{};     ///< QQ number of the member who left.
    };

    /**
     * @brief Fields of a `group_increase` notice: a member joined, by `NoticeEvent::sub_type` `approve` or `invite`.
     */
    struct GroupIncreaseNotice {
        std::int64_t group_id{};    ///< Group number.
        std::int64_t operator_id{}; ///< QQ number of the admin who approved or the member who invited; NapCat sends 0 when it cannot tell.
        std::int64_t user_id{};     ///< QQ number of the member who joined, the bot account itself when it joined.
    };

    /**
     * @brief Fields of a `group_ban` notice: a member was muted or unmuted, by `NoticeEvent::sub_type` `ban` or `lift_ban`.
     */
    struct GroupBanNotice {
        std::int64_t group_id{};    ///< Group number.
        std::int64_t operator_id{}; ///< QQ number of the admin who changed the mute.
        std::int64_t user_id{};     ///< QQ number of the muted member; NapCat sends 0 for the whole group.
        std::int64_t duration{};    ///< Seconds of the mute; 0 when it is lifted, and NapCat's -1 for muting the whole group.
    };

    /**
     * @brief Fields of a `friend_add` notice: the bot account has a new friend.
     */
    struct FriendAddNotice {
        std::int64_t user_id{}; ///< QQ number of the new friend.
    };

    /**
     * @brief Fields of a `group_recall` notice: a group message was recalled.
     */
    struct GroupRecallNotice {
        std::int64_t group_id{};    ///< Group number.
        std::int64_t user_id{};     ///< QQ number of the member who sent the message.
        std::int64_t operator_id{}; ///< QQ number of the member who recalled it: the sender or an admin.
        std::int32_t message_id{};  ///< ID of the recalled message, as in its `MessageEvent::message_id`.
    };

    /**
     * @brief Fields of a `friend_recall` notice: a private message was recalled.
     */
    struct FriendRecallNotice {
        std::int64_t user_id{};    ///< QQ number of the friend who sent the message.
        std::int32_t message_id{}; ///< ID of the recalled message, as in its `MessageEvent::message_id`.
    };

    /**
     * @brief Fields of a `notify` notice with `sub_type` `poke`: someone poked someone.
     */
    struct PokeNotice {
        std::optional<std::int64_t> group_id;  ///< Group number of a group poke; empty for NapCat's friend pokes.
        std::int64_t user_id{};                ///< QQ number of who poked, in a group; in NapCat's friend pokes, the friend of the chat.
        std::optional<std::int64_t> sender_id; ///< QQ number of who poked, in NapCat's friend pokes; empty in group pokes, where it is `user_id`.
        std::int64_t target_id{};              ///< QQ number of who was poked.
    };

    /**
     * @brief Type-specific fields of a notice, chosen by its `notice_type` and `sub_type`.
     *
     * `std::monostate` for any other notice, such as NapCat's extensions, whose fields stay in `NoticeEvent::raw`.
     */
    using NoticeDetail = std::variant<std::monostate, GroupUploadNotice, GroupAdminNotice, GroupDecreaseNotice, GroupIncreaseNotice, GroupBanNotice, FriendAddNotice, GroupRecallNotice, FriendRecallNotice, PokeNotice>;

    /**
     * @brief Event with `post_type` `notice`, e.g. a group member joining or a message being recalled.
     *
     * Read the type-specific fields from `detail`:
     * @code
     * if (auto const* upload{ std::get_if<drogon_onebot::GroupUploadNotice>(&event.detail) }) {
     *     LOG_INFO << upload->user_id << " uploaded " << upload->file.name;
     * }
     * @endcode
     */
    struct NoticeEvent {
        std::int64_t time{};     ///< Unix time in seconds when the event happened.
        std::int64_t self_id{};  ///< QQ number of the bot account that received the event.
        std::string notice_type; ///< e.g. `group_increase`, `group_recall`, or `notify`.
        std::string sub_type;    ///< Subtype within `notice_type`, e.g. `approve` or `poke`; empty when the type has none.
        NoticeDetail detail;     ///< Type-specific fields; `std::monostate` for a notice without a type in `NoticeDetail`.
        Json::Value raw;         ///< Whole event as received, including the type-specific fields.
    };

    /**
     * @brief Event with `post_type` `request`: a friend request or a group join request or invitation.
     */
    struct RequestEvent {
        std::int64_t time{};      ///< Unix time in seconds when the event happened.
        std::int64_t self_id{};   ///< QQ number of the bot account that received the event.
        std::string request_type; ///< `friend` or `group`.
        std::string sub_type;     ///< `add` or `invite` for group requests; empty for friend requests.
        std::int64_t user_id{};   ///< QQ number of who sent the request: the requester, or the inviter of an invitation.
        std::string comment;      ///< Verification message; empty when absent.
        std::string flag;         ///< ID of the request, for `OneBot::SetFriendAddRequest()` or `OneBot::SetGroupAddRequest()`.
        Json::Value raw;          ///< Whole event as received, for fields without a member here.
    };

    /**
     * @brief Event with `post_type` `meta_event`: the OneBot implementation's lifecycle and heartbeat.
     */
    struct MetaEvent {
        std::int64_t time{};         ///< Unix time in seconds when the event happened.
        std::int64_t self_id{};      ///< QQ number of the bot account that sent the event.
        std::string meta_event_type; ///< `lifecycle` or `heartbeat`.
        std::string sub_type;        ///< `enable`, `disable`, or `connect` for lifecycle events; empty for heartbeats.
        Json::Value raw;             ///< Whole event as received, e.g. `status` and `interval` of a heartbeat.
    };

} // namespace drogon_onebot
