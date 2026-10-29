/**
 * @file field_reader.cpp
 * @brief Implement the reader that event parsing and the typed actions use to read typed JSON members.
 */
#include "details/field_reader.hpp"

#include <utility>

namespace drogon_onebot::details {

    /**
     * @brief Read members of `object`.
     * @param object Object to read; a non-object reads as an object without members.
     */
    FieldReader::FieldReader(Json::Value const& object)
        : m_object{ object } {}

    /**
     * @brief Read an optional string member.
     * @param key Member name.
     * @return The string, or empty when the member is absent or invalid.
     */
    auto FieldReader::String(char const* key) -> std::string {
        auto const& value{ member(m_object, key) };
        if (value.isNull()) {
            return {};
        }
        if (!value.isString()) {
            fail(std::format("{} is not a string", key));
            return {};
        }
        return value.asString();
    }

    /**
     * @brief Read a required string member.
     * @param key Member name.
     * @return The string, or empty after recording a problem.
     */
    auto FieldReader::RequiredString(char const* key) -> std::string {
        if (member(m_object, key).isNull()) {
            fail(std::format("{} is missing", key));
            return {};
        }
        return String(key);
    }

    /**
     * @brief Read an optional boolean member.
     * @param key Member name.
     * @return The boolean, or false when the member is absent or invalid.
     */
    auto FieldReader::Bool(char const* key) -> bool {
        auto const& value{ member(m_object, key) };
        if (value.isNull()) {
            return false;
        }
        if (!value.isBool()) {
            fail(std::format("{} is not a boolean", key));
            return false;
        }
        return value.asBool();
    }

    /**
     * @brief Read a required boolean member.
     * @param key Member name.
     * @return The boolean, or false after recording a problem.
     */
    auto FieldReader::RequiredBool(char const* key) -> bool {
        if (member(m_object, key).isNull()) {
            fail(std::format("{} is missing", key));
            return false;
        }
        return Bool(key);
    }

    /**
     * @brief Read a boolean member that may be null, keeping absent apart from false.
     * @param key Member name.
     * @return The boolean, or empty when the member is absent; false after recording a problem.
     */
    auto FieldReader::NullableBool(char const* key) -> std::optional<bool> {
        if (member(m_object, key).isNull()) {
            return std::nullopt;
        }
        return Bool(key);
    }

    /**
     * @brief Read a required message member in array format.
     * @param key Member name.
     * @return The segments, or none after recording a problem.
     */
    auto FieldReader::Segments(char const* key) -> Message {
        auto message{ parse_message(member(m_object, key)) };
        if (!message) {
            fail(std::format("{} is not an array of message segments (set messagePostFormat to array in NapCat)", key));
            return {};
        }
        return std::move(*message);
    }

    /**
     * @brief Return the first problem found.
     * @return Description of the problem; empty while every read succeeded.
     */
    auto FieldReader::Problem() const noexcept -> std::string const& {
        return m_problem;
    }

    /**
     * @brief Record a problem unless an earlier one is already recorded.
     * @param problem Description of the problem.
     */
    auto FieldReader::fail(std::string problem) -> void {
        if (m_problem.empty()) {
            m_problem = std::move(problem);
        }
    }

    /**
     * @brief Read a running status: the `data` of `get_status`, or the `status` of a heartbeat.
     * @param value Object to read.
     * @return The status, or the first problem found, e.g. `good is missing`.
     */
    auto read_status(Json::Value const& value) -> std::expected<Status, std::string> {
        FieldReader fields{ value };
        Status status{
            .online = fields.NullableBool("online"),
            .good = fields.RequiredBool("good"),
            .raw = value,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return status;
    }

} // namespace drogon_onebot::details
