/**
 * @file field_reader.hpp
 * @brief Declare the reader that event parsing and the typed actions use to read typed JSON members and arrays.
 */
#pragma once

#include <concepts>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <json/value.h>

#include "details/json.hpp"
#include "drogon_onebot/types/message.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Read typed members of one JSON object, keeping the first problem found.
     *
     * A member that is missing or null reads as absent; a member of the wrong type is a problem.
     */
    class FieldReader {
    private:
        Json::Value const& m_object; ///< Object to read; outlives the reader.
        std::string m_problem;       ///< First problem found; empty while every read succeeded.

    public:
        /**
         * @brief Read members of `object`.
         * @param object Object to read; a non-object reads as an object without members.
         */
        explicit FieldReader(Json::Value const& object);

        /**
         * @brief Read an optional integer member.
         * @tparam T Integer type to return.
         * @param key Member name.
         * @return The integer, or empty when the member is absent or invalid.
         */
        template <std::integral T>
        [[nodiscard]] auto Integer(char const* key) -> std::optional<T> {
            auto const& value{ member(m_object, key) };
            if (value.isNull()) {
                return std::nullopt;
            }
            auto const number{ read_integer<T>(value) };
            if (!number) {
                fail(std::format("{} is not an integer in range", key));
            }
            return number;
        }

        /**
         * @brief Read a required integer member.
         * @tparam T Integer type to return.
         * @param key Member name.
         * @return The integer, or 0 after recording a problem.
         */
        template <std::integral T>
        [[nodiscard]] auto RequiredInteger(char const* key) -> T {
            auto const number{ Integer<T>(key) };
            if (!number) {
                fail(std::format("{} is missing", key));
            }
            return number.value_or(T{});
        }

        /**
         * @brief Read an optional string member.
         * @param key Member name.
         * @return The string, or empty when the member is absent or invalid.
         */
        [[nodiscard]] auto String(char const* key) -> std::string;

        /**
         * @brief Read a required string member.
         * @param key Member name.
         * @return The string, or empty after recording a problem.
         */
        [[nodiscard]] auto RequiredString(char const* key) -> std::string;

        /**
         * @brief Read an optional boolean member.
         * @param key Member name.
         * @return The boolean, or false when the member is absent or invalid.
         */
        [[nodiscard]] auto Bool(char const* key) -> bool;

        /**
         * @brief Read a required boolean member.
         * @param key Member name.
         * @return The boolean, or false after recording a problem.
         */
        [[nodiscard]] auto RequiredBool(char const* key) -> bool;

        /**
         * @brief Read a required message member in array format.
         * @param key Member name.
         * @return The segments, or none after recording a problem.
         */
        [[nodiscard]] auto Segments(char const* key) -> Message;

        /**
         * @brief Return the first problem found.
         * @return Description of the problem; empty while every read succeeded.
         */
        [[nodiscard]] auto Problem() const noexcept -> std::string const&;

    private:
        /**
         * @brief Record a problem unless an earlier one is already recorded.
         * @param problem Description of the problem.
         */
        auto fail(std::string problem) -> void;
    };

    /**
     * @brief Read every element of a JSON array, stopping at the first problem.
     * @tparam T Element type to return.
     * @tparam Read Callable that reads one element into `T` or describes its first problem.
     * @param array Value to read.
     * @param name Name of the array, for problems, e.g. `data` or `messages`.
     * @param read Reader of one element.
     * @return The elements in order, or the first problem, e.g. `data is missing`,
     * `data is not an array`, or `data[2].user_id is missing`.
     */
    template <typename T, typename Read>
    requires std::is_invocable_r_v<std::expected<T, std::string>, Read&, Json::Value const&>
    [[nodiscard]] auto read_array(
        Json::Value const& array,
        std::string_view name,
        Read read
    ) -> std::expected<std::vector<T>, std::string> {
        if (array.isNull()) {
            return std::unexpected{ std::format("{} is missing", name) };
        }
        if (!array.isArray()) {
            return std::unexpected{ std::format("{} is not an array", name) };
        }
        std::vector<T> items;
        items.reserve(array.size());
        for (Json::ArrayIndex index{}; index < array.size(); ++index) {
            std::expected<T, std::string> item{ read(array[index]) };
            if (!item) {
                return std::unexpected{ std::format("{}[{}].{}", name, index, item.error()) };
            }
            items.push_back(std::move(*item));
        }
        return items;
    }

} // namespace drogon_onebot::details
