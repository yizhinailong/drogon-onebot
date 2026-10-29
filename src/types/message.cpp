/**
 * @file message.cpp
 * @brief Implement the functions that build common message segments.
 */
#include "drogon_onebot/types/message.hpp"

#include <utility>

namespace {

    /**
     * @brief Build a segment whose data has one member.
     * @param type Segment type.
     * @param key Name of the data member.
     * @param value Value of the data member.
     * @return `{ "type": <type>, "data": { <key>: <value> } }`.
     */
    auto segment(
        char const* type,
        char const* key,
        Json::Value value
    ) -> drogon_onebot::MessageSegment {
        drogon_onebot::MessageSegment result{ .type = type };
        result.data[key] = std::move(value);
        return result;
    }

    /**
     * @brief Build a segment that recommends a user or a group.
     * @param type `qq` for a user or `group` for a group.
     * @param id QQ number or group number to recommend.
     * @return `{ "type": "contact", "data": { "type": <type>, "id": "<id>" } }`.
     */
    auto contact(
        char const* type,
        std::int64_t id
    ) -> drogon_onebot::MessageSegment {
        auto result{ segment("contact", "type", type) };
        result.data["id"] = std::to_string(id);
        return result;
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Build a plain text segment.
     * @param text Text to send as is.
     * @return `{ "type": "text", "data": { "text": <text> } }`.
     */
    auto MessageSegment::Text(std::string text) -> MessageSegment {
        return segment("text", "text", std::move(text));
    }

    /**
     * @brief Build a segment that mentions one group member.
     * @param user_id QQ number of the member to mention.
     * @return `{ "type": "at", "data": { "qq": "<user_id>" } }`.
     * @note NapCat drops `at` segments from private messages.
     */
    auto MessageSegment::At(std::int64_t user_id) -> MessageSegment {
        return segment("at", "qq", std::to_string(user_id));
    }

    /**
     * @brief Build a segment that mentions every group member.
     * @return `{ "type": "at", "data": { "qq": "all" } }`.
     */
    auto MessageSegment::AtAll() -> MessageSegment {
        return segment("at", "qq", "all");
    }

    /**
     * @brief Build a segment that quotes an earlier message, usually put first.
     * @param message_id ID of the quoted message, e.g. `MessageEvent::message_id`.
     * @return `{ "type": "reply", "data": { "id": "<message_id>" } }`.
     * @note NapCat drops the segment, and still sends the rest, when it does not know `message_id`.
     */
    auto MessageSegment::Reply(std::int32_t message_id) -> MessageSegment {
        return segment("reply", "id", std::to_string(message_id));
    }

    /**
     * @brief Build an image segment.
     * @param file Image to send: an `http://` or `https://` URL, a `file://` URI or
     * path on the OneBot implementation's host, or `base64://` followed by the image data.
     * @return `{ "type": "image", "data": { "file": <file> } }`.
     */
    auto MessageSegment::Image(std::string file) -> MessageSegment {
        return segment("image", "file", std::move(file));
    }

    /**
     * @brief Build a QQ face (emoticon) segment.
     * @param id Face ID.
     * @return `{ "type": "face", "data": { "id": "<id>" } }`.
     * @note NapCat drops a face whose ID it does not know.
     */
    auto MessageSegment::Face(std::int32_t id) -> MessageSegment {
        return segment("face", "id", std::to_string(id));
    }

    /**
     * @brief Build a voice segment.
     * @param file Voice to send, in the forms that `Image()` takes, or the `file` of a received `record` segment.
     * @return `{ "type": "record", "data": { "file": <file> } }`.
     * @note NapCat converts the voice to QQ's SILK format with ffmpeg, and fails the whole
     * send when it cannot fetch or convert the file.
     */
    auto MessageSegment::Record(std::string file) -> MessageSegment {
        return segment("record", "file", std::move(file));
    }

    /**
     * @brief Build a short video segment.
     * @param file Video to send, in the forms that `Image()` takes, or the `file` of a received `video` segment.
     * @return `{ "type": "video", "data": { "file": <file> } }`.
     * @note NapCat takes the cover from the video with ffmpeg, or a default one when it cannot;
     * set `data["thumb"]` to send another. It fails the whole send when it cannot fetch the file.
     */
    auto MessageSegment::Video(std::string file) -> MessageSegment {
        return segment("video", "file", std::move(file));
    }

    /**
     * @brief Build a segment that recommends a QQ user.
     * @param user_id QQ number of the user to recommend.
     * @return `{ "type": "contact", "data": { "type": "qq", "id": "<user_id>" } }`.
     * @note NapCat sends the recommendation card that QQ builds for the user.
     */
    auto MessageSegment::ContactFriend(std::int64_t user_id) -> MessageSegment {
        return contact("qq", user_id);
    }

    /**
     * @brief Build a segment that recommends a group.
     * @param group_id Group number of the group to recommend.
     * @return `{ "type": "contact", "data": { "type": "group", "id": "<group_id>" } }`.
     * @note NapCat sends the recommendation card that QQ builds for the group.
     */
    auto MessageSegment::ContactGroup(std::int64_t group_id) -> MessageSegment {
        return contact("group", group_id);
    }

    /**
     * @brief Build a JSON (card) segment.
     * @param data Content of the card, a JSON document as a string.
     * @return `{ "type": "json", "data": { "data": <data> } }`.
     * @note NapCat passes `data` to QQ as the card's content as is.
     */
    auto MessageSegment::Json(std::string data) -> MessageSegment {
        return segment("json", "data", std::move(data));
    }

    /**
     * @brief Build a segment that shares a song from a music platform.
     * @param type Platform: `qq`, `163`, or `xm` in the standard; NapCat takes `qq`, `163`, `kugou`, `kuwo`, and `migu`.
     * @param id Song ID on that platform, e.g. `28949129`.
     * @return `{ "type": "music", "data": { "type": <type>, "id": <id> } }`.
     * @note NapCat sends `type` and `id` to an outside service that signs the card, and drops
     * the segment when signing fails.
     */
    auto MessageSegment::Music(
        std::string type,
        std::string id
    ) -> MessageSegment {
        auto result{ segment("music", "type", std::move(type)) };
        result.data["id"] = std::move(id);
        return result;
    }

    /**
     * @brief Build a rock-paper-scissors magic emoticon segment, whose result QQ picks.
     * @return `{ "type": "rps", "data": { "result": "" } }`.
     * @note The standard sends no data, but NapCat rejects an `rps` segment without `result`.
     * It ignores the value; the empty string is no real result.
     */
    auto MessageSegment::Rps() -> MessageSegment {
        return segment("rps", "result", "");
    }

} // namespace drogon_onebot
