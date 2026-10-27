/**
 * @file json.hpp
 * @brief Declare helpers that read OneBot JSON values and convert messages.
 */
#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <json/value.h>

#include "drogon_onebot/error.hpp"
#include "drogon_onebot/types/message.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Look up a member without jsoncpp's assertion on non-objects.
     * @param object Value that should be an object.
     * @param key Member name.
     * @return The member, or null when `object` is not an object or has no such member.
     */
    [[nodiscard]] auto member(Json::Value const& object, char const* key) -> Json::Value const&;

    /**
     * @brief Read an integer sent as a JSON number or as a decimal string.
     *
     * NapCat sends some IDs, such as `user_id` and `group_id` in events, as strings.
     * @param value Value to read.
     * @return The integer, or empty when `value` is neither an integral number nor a decimal string within range.
     */
    [[nodiscard]] auto read_int64(Json::Value const& value) -> std::optional<std::int64_t>;

    /**
     * @brief Read an integer sent as a JSON number or as a decimal string, and narrow it to `T`.
     * @tparam T Integer type to return.
     * @param value Value to read.
     * @return The integer, or empty when `value` is not an integer or does not fit in `T`.
     */
    template <std::integral T>
    [[nodiscard]] auto read_integer(Json::Value const& value) -> std::optional<T> {
        auto const number{ read_int64(value) };
        if (!number || !std::in_range<T>(*number)) {
            return std::nullopt;
        }
        return static_cast<T>(*number);
    }

    /**
     * @brief Serialize a message into OneBot v11 array format.
     * @param message Segments to serialize.
     * @return Array of `{ "type": ..., "data": ... }` objects.
     */
    [[nodiscard]] auto to_json(Message const& message) -> Json::Value;

    /**
     * @brief Read a message in OneBot v11 array format.
     * @param value Value to read.
     * @return The segments, or empty when `value` is not an array of objects that each
     * have a `type` string and an object or missing `data`.
     */
    [[nodiscard]] auto parse_message(Json::Value const& value) -> std::optional<Message>;

    /**
     * @brief Describe a successful action whose `data` lacks a field of the expected type.
     * @param action Action name, for the message.
     * @param field Missing or malformed field of `data`.
     * @param data The envelope's `data`.
     * @return A `ResponseValidation` error that carries `data` as its body.
     */
    [[nodiscard]] auto invalid_data_error(
        std::string_view action,
        std::string_view field,
        Json::Value const& data
    ) -> Error;

} // namespace drogon_onebot::details
