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
     * @brief Type-specific fields of a notice, chosen by its `notice_type` and `sub_type`.
     *
     * `std::monostate` for any other notice, such as NapCat's extensions, whose fields stay in `NoticeEvent::raw`.
     */
    using NoticeDetail = std::variant<std::monostate, GroupUploadNotice>;

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
        Json::Value raw;          ///< Whole event as received, including `flag` for answering the request.
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
