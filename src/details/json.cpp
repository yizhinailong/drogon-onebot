/**
 * @file json.cpp
 * @brief Implement helpers that read OneBot JSON values and convert messages.
 */
#include "details/json.hpp"

#include <charconv>
#include <format>
#include <string>
#include <system_error>

namespace drogon_onebot::details {

    /**
     * @brief Look up a member without jsoncpp's assertion on non-objects.
     * @param object Value that should be an object.
     * @param key Member name.
     * @return The member, or null when `object` is not an object or has no such member.
     */
    auto member(Json::Value const& object, char const* key) -> Json::Value const& {
        if (!object.isObject()) {
            return Json::Value::nullSingleton();
        }
        return object[key];
    }

    /**
     * @brief Read an integer sent as a JSON number or as a decimal string.
     * @param value Value to read.
     * @return The integer, or empty when `value` is neither an integral number nor a decimal string within range.
     */
    auto read_int64(Json::Value const& value) -> std::optional<std::int64_t> {
        if (value.isInt64()) {
            return value.asInt64();
        }
        if (value.isString()) {
            auto const text{ value.asString() };
            std::int64_t number{};
            auto const [end, error]{ std::from_chars(text.data(), text.data() + text.size(), number) };
            if (error == std::errc{} && end == text.data() + text.size()) {
                return number;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Serialize a message into OneBot v11 array format.
     * @param message Segments to serialize.
     * @return Array of `{ "type": ..., "data": ... }` objects.
     */
    auto to_json(Message const& message) -> Json::Value {
        Json::Value segments{ Json::arrayValue };
        for (auto const& segment : message) {
            Json::Value item{ Json::objectValue };
            item["type"] = segment.type;
            item["data"] = segment.data;
            segments.append(std::move(item));
        }
        return segments;
    }

    /**
     * @brief Read a message in OneBot v11 array format.
     * @param value Value to read.
     * @return The segments, or empty when `value` is not an array of objects that each
     * have a `type` string and an object or missing `data`.
     */
    auto parse_message(Json::Value const& value) -> std::optional<Message> {
        if (!value.isArray()) {
            return std::nullopt;
        }
        Message message{};
        message.reserve(value.size());
        for (auto const& item : value) {
            auto const& type{ member(item, "type") };
            auto const& data{ member(item, "data") };
            if (!type.isString() || !(data.isObject() || data.isNull())) {
                return std::nullopt;
            }
            message.push_back(MessageSegment{
                .type = type.asString(),
                .data = data.isNull() ? Json::Value{ Json::objectValue } : data,
            });
        }
        return message;
    }

    /**
     * @brief Describe a successful action whose `data` lacks a field of the expected type.
     * @param action Action name, for the message.
     * @param field Missing or malformed field of `data`.
     * @param data The envelope's `data`.
     * @return A `ResponseValidation` error that carries `data` as its body.
     */
    auto invalid_data_error(
        std::string_view action,
        std::string_view field,
        Json::Value const& data
    ) -> Error {
        return Error{
            .kind = ErrorKind::ResponseValidation,
            .message = std::format("{}: response data has no valid {}", action, field),
            .body = data.toStyledString(),
        };
    }

} // namespace drogon_onebot::details
