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

} // namespace drogon_onebot
