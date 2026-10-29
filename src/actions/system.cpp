/**
 * @file system.cpp
 * @brief Implement the actions of `OneBot` about the OneBot implementation itself.
 */
#include <expected>
#include <string>
#include <utility>

#include "details/field_reader.hpp"
#include "details/json.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {

    /**
     * @brief Read the `data` of a `can_send_*` action.
     * @param data Object to read.
     * @return The answer, or the first problem found, e.g. `yes is missing`.
     */
    auto to_can_send(Json::Value const& data) -> std::expected<drogon_onebot::CanSend, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::CanSend answer{
            .yes = fields.RequiredBool("yes"),
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return answer;
    }

    /**
     * @brief Read the `data` of `get_status`.
     * @param data Object to read.
     * @return The status, or the first problem found, e.g. `good is missing`.
     */
    auto to_status(Json::Value const& data) -> std::expected<drogon_onebot::Status, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::Status status{
            .online = fields.NullableBool("online"),
            .good = fields.RequiredBool("good"),
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return status;
    }

    /**
     * @brief Read the `data` of `get_version_info`.
     * @param data Object to read.
     * @return The version, or the first problem found, e.g. `app_name is missing`.
     */
    auto to_version_info(Json::Value const& data) -> std::expected<drogon_onebot::VersionInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::VersionInfo info{
            .app_name = fields.RequiredString("app_name"),
            .app_version = fields.String("app_version"),
            .protocol_version = fields.String("protocol_version"),
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return info;
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `can_send_image`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding whether the implementation can send images, or why the action failed.
     * @note NapCat always answers yes.
     */
    auto OneBot::CanSendImage(
        CanSendImageParams params
    ) const -> drogon::Task<Result<CanSend>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("can_send_image", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        auto const answer{ to_can_send(*data) };
        if (!answer) {
            co_return std::unexpected{ details::invalid_data_error("can_send_image", answer.error(), *data) };
        }
        co_return *answer;
    }

    /**
     * @brief Call `can_send_record`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding whether the implementation can send voice, or why the action failed.
     * @note NapCat always answers yes.
     */
    auto OneBot::CanSendRecord(
        CanSendRecordParams params
    ) const -> drogon::Task<Result<CanSend>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("can_send_record", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        auto const answer{ to_can_send(*data) };
        if (!answer) {
            co_return std::unexpected{ details::invalid_data_error("can_send_record", answer.error(), *data) };
        }
        co_return *answer;
    }

    /**
     * @brief Call `get_status`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the implementation's running status, or why the action failed.
     * @note NapCat always sends `good` as true, even while the account is offline, so check `online`.
     */
    auto OneBot::GetStatus(
        GetStatusParams params
    ) const -> drogon::Task<Result<Status>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("get_status", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        auto status{ to_status(*data) };
        if (!status) {
            co_return std::unexpected{ details::invalid_data_error("get_status", status.error(), *data) };
        }
        co_return std::move(*status);
    }

    /**
     * @brief Call `get_version_info`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the implementation's name and version, or why the action failed.
     */
    auto OneBot::GetVersionInfo(
        GetVersionInfoParams params
    ) const -> drogon::Task<Result<VersionInfo>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("get_version_info", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        auto info{ to_version_info(*data) };
        if (!info) {
            co_return std::unexpected{ details::invalid_data_error("get_version_info", info.error(), *data) };
        }
        co_return std::move(*info);
    }

    /**
     * @brief Call `set_restart`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing once the implementation accepted the restart, or why the action failed.
     * @note NapCat answers before it restarts its worker process, so its HTTP API and event
     * posts pause until the new process is up. Running in a single process or as its framework
     * build, it cannot restart itself and fails with `ErrorKind::Action`.
     */
    auto OneBot::SetRestart(
        SetRestartParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["delay"] = params.delay;
        auto const data{ co_await transport->Call("set_restart", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `clean_cache`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing once the implementation cleaned its cache, or why the action failed.
     * @note NapCat deletes the files directly inside its temp directory and QQ's media and log
     * directories, not their subdirectories. It only logs the files it cannot delete, and fails
     * with `ErrorKind::Action` only when it cannot read its temp directory.
     */
    auto OneBot::CleanCache(
        CleanCacheParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("clean_cache", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

} // namespace drogon_onebot
