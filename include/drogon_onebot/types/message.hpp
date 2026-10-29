/**
 * @file message.hpp
 * @brief Declare the message segment type and the request and response types of the message actions.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief One segment of a message in OneBot v11 array format.
     *
     * Serialized as `{ "type": <type>, "data": <data> }`. The static functions build the
     * common segment types, as NoneBot2's and aiocqhttp's `MessageSegment` do:
     * @code
     * drogon_onebot::Message const reply{
     *     drogon_onebot::MessageSegment::Reply(event.message_id),
     *     drogon_onebot::MessageSegment::At(event.user_id),
     *     drogon_onebot::MessageSegment::Text(" pong"),
     * };
     * @endcode
     * Build other types, or add optional fields, through `type` and `data` directly.
     */
    struct MessageSegment {
        std::string type;                      ///< Segment type, e.g. `text`, `face`, `image`, or `at`.
        Json::Value data{ Json::objectValue }; ///< Segment fields, e.g. `{ "text": "Hello!" }` for `text`.

        /**
         * @brief Build a plain text segment.
         * @param text Text to send as is.
         * @return `{ "type": "text", "data": { "text": <text> } }`.
         */
        [[nodiscard]] static auto Text(std::string text) -> MessageSegment;

        /**
         * @brief Build a segment that mentions one group member.
         * @param user_id QQ number of the member to mention.
         * @return `{ "type": "at", "data": { "qq": "<user_id>" } }`.
         * @note NapCat drops `at` segments from private messages.
         */
        [[nodiscard]] static auto At(std::int64_t user_id) -> MessageSegment;

        /**
         * @brief Build a segment that mentions every group member.
         * @return `{ "type": "at", "data": { "qq": "all" } }`.
         */
        [[nodiscard]] static auto AtAll() -> MessageSegment;

        /**
         * @brief Build a segment that quotes an earlier message, usually put first.
         * @param message_id ID of the quoted message, e.g. `MessageEvent::message_id`.
         * @return `{ "type": "reply", "data": { "id": "<message_id>" } }`.
         * @note NapCat drops the segment, and still sends the rest, when it does not know `message_id`.
         */
        [[nodiscard]] static auto Reply(std::int32_t message_id) -> MessageSegment;

        /**
         * @brief Build an image segment.
         * @param file Image to send: an `http://` or `https://` URL, a `file://` URI or
         * path on the OneBot implementation's host, or `base64://` followed by the image data.
         * @return `{ "type": "image", "data": { "file": <file> } }`.
         */
        [[nodiscard]] static auto Image(std::string file) -> MessageSegment;

        /**
         * @brief Build a QQ face (emoticon) segment.
         * @param id Face ID.
         * @return `{ "type": "face", "data": { "id": "<id>" } }`.
         * @note NapCat drops a face whose ID it does not know.
         */
        [[nodiscard]] static auto Face(std::int32_t id) -> MessageSegment;

        /**
         * @brief Build a voice segment.
         * @param file Voice to send, in the forms that `Image()` takes, or the `file` of a received `record` segment.
         * @return `{ "type": "record", "data": { "file": <file> } }`.
         * @note NapCat converts the voice to QQ's SILK format with ffmpeg, and fails the whole
         * send when it cannot fetch or convert the file.
         */
        [[nodiscard]] static auto Record(std::string file) -> MessageSegment;

        /**
         * @brief Build a short video segment.
         * @param file Video to send, in the forms that `Image()` takes, or the `file` of a received `video` segment.
         * @return `{ "type": "video", "data": { "file": <file> } }`.
         * @note NapCat takes the cover from the video with ffmpeg, or a default one when it cannot;
         * set `data["thumb"]` to send another. It fails the whole send when it cannot fetch the file.
         */
        [[nodiscard]] static auto Video(std::string file) -> MessageSegment;

        /**
         * @brief Build a segment that recommends a QQ user.
         * @param user_id QQ number of the user to recommend.
         * @return `{ "type": "contact", "data": { "type": "qq", "id": "<user_id>" } }`.
         * @note NapCat sends the recommendation card that QQ builds for the user.
         */
        [[nodiscard]] static auto ContactFriend(std::int64_t user_id) -> MessageSegment;

        /**
         * @brief Build a segment that recommends a group.
         * @param group_id Group number of the group to recommend.
         * @return `{ "type": "contact", "data": { "type": "group", "id": "<group_id>" } }`.
         * @note NapCat sends the recommendation card that QQ builds for the group.
         */
        [[nodiscard]] static auto ContactGroup(std::int64_t group_id) -> MessageSegment;

        /**
         * @brief Build a JSON (card) segment.
         * @param data Content of the card, a JSON document as a string.
         * @return `{ "type": "json", "data": { "data": <data> } }`.
         * @note NapCat passes `data` to QQ as the card's content as is.
         */
        [[nodiscard]] static auto Json(std::string data) -> MessageSegment;
    };

    /**
     * @brief Message as an ordered list of segments.
     */
    using Message = std::vector<MessageSegment>;

    /**
     * @brief Sender of a message, from its `sender` object.
     */
    struct Sender {
        std::int64_t user_id{}; ///< QQ number of the sender.
        std::string nickname;   ///< Nickname of the sender.
        std::string card;       ///< Group card in group messages; empty when unset or in private messages.
        std::string role;       ///< `owner`, `admin`, or `member` in group messages; empty in private messages.
    };

    /**
     * @brief Arguments of `OneBot::SendPrivateMsg()`.
     */
    struct SendPrivateMsgParams {
        std::int64_t user_id{}; ///< QQ number of the recipient.
        Message message;        ///< Message to send.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SendGroupMsg()`.
     */
    struct SendGroupMsgParams {
        std::int64_t group_id{}; ///< Group number to send to.
        Message message;         ///< Message to send.
        double timeout{ 30.0 };  ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SendMsg()`.
     *
     * The fields mirror `MessageEvent`, so a handler can answer either kind of message
     * with the event's own `message_type`, `user_id`, and `group_id`.
     */
    struct SendMsgParams {
        std::string message_type;             ///< `private` or `group`; empty lets the implementation choose from the IDs.
        std::optional<std::int64_t> user_id;  ///< QQ number of the recipient of a private message; empty sends none.
        std::optional<std::int64_t> group_id; ///< Group number to send to; empty sends none.
        Message message;                      ///< Message to send.
        double timeout{ 30.0 };               ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Message the OneBot implementation sent, from `send_private_msg`, `send_group_msg`, or `send_msg`.
     */
    struct SentMessage {
        std::int32_t message_id{}; ///< ID of the sent message, e.g. for `delete_msg`.
    };

    /**
     * @brief Arguments of `OneBot::DeleteMsg()`.
     */
    struct DeleteMsgParams {
        std::int32_t message_id{}; ///< ID of the message to recall.
        double timeout{ 30.0 };    ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::GetMsg()`.
     */
    struct GetMsgParams {
        std::int32_t message_id{}; ///< ID of the message to fetch, e.g. the `id` of a received `reply` segment.
        double timeout{ 30.0 };    ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Message fetched from the OneBot implementation, from `get_msg`.
     */
    struct MessageInfo {
        std::int64_t time{};                  ///< Unix time in seconds when the message was sent.
        std::string message_type;             ///< `private` or `group`.
        std::int32_t message_id{};            ///< ID of the message.
        std::int32_t real_id{};               ///< The implementation's own ID of the message; 0 when absent.
        std::optional<std::int64_t> group_id; ///< Group number of a group message, or NapCat's source group of a temporary private chat; empty otherwise.
        Sender sender;                        ///< Sender details; `role` only for group messages.
        Message message;                      ///< Message content in array format.
        std::string raw_message;              ///< Message content as a CQ-code string; empty when absent.
        Json::Value raw;                      ///< Whole `data` as received, for fields without a member here.
    };

    /**
     * @brief Arguments of `OneBot::GetForwardMsg()`.
     */
    struct GetForwardMsgParams {
        std::string id;         ///< ID of the forward: the `id` of a received `forward` segment.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Content of a forward, from `get_forward_msg`.
     */
    struct ForwardMessage {
        std::vector<MessageInfo> messages; ///< Forwarded messages in order; a nested forward is a `forward` segment that carries its messages in `data["content"]`.
    };

} // namespace drogon_onebot
