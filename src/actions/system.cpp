/**
 * @file system.cpp
 * @brief Implement the actions of `OneBot` about the OneBot implementation itself.
 */
#include <expected>
#include <string>
#include <utility>

#include "details/action_call.hpp"
#include "details/field_reader.hpp"
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
     * @param params Request fields, read before this returns.
     * @return Task yielding whether the implementation can send images, or why the action failed.
     * @note NapCat always answers yes.
     */
    auto OneBot::CanSendImage(
        CanSendImageParams params
    ) const -> drogon::Task<Result<CanSend>> {
        return details::call_and_read<CanSend>(m_transport, "can_send_image", Json::Value{ Json::objectValue }, params.timeout, to_can_send);
    }

    /**
     * @brief Call `can_send_record`.
     * @param params Request fields, read before this returns.
     * @return Task yielding whether the implementation can send voice, or why the action failed.
     * @note NapCat always answers yes.
     */
    auto OneBot::CanSendRecord(
        CanSendRecordParams params
    ) const -> drogon::Task<Result<CanSend>> {
        return details::call_and_read<CanSend>(m_transport, "can_send_record", Json::Value{ Json::objectValue }, params.timeout, to_can_send);
    }

    /**
     * @brief Call `get_status`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the implementation's running status, or why the action failed.
     * @note NapCat always sends `good` as true, even while the account is offline, so check `online`.
     */
    auto OneBot::GetStatus(
        GetStatusParams params
    ) const -> drogon::Task<Result<Status>> {
        return details::call_and_read<Status>(m_transport, "get_status", Json::Value{ Json::objectValue }, params.timeout, details::read_status);
    }

    /**
     * @brief Call `get_version_info`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the implementation's name and version, or why the action failed.
     */
    auto OneBot::GetVersionInfo(
        GetVersionInfoParams params
    ) const -> drogon::Task<Result<VersionInfo>> {
        return details::call_and_read<VersionInfo>(m_transport, "get_version_info", Json::Value{ Json::objectValue }, params.timeout, to_version_info);
    }

    /**
     * @brief Call `set_restart`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing once the implementation accepted the restart, or why the action failed.
     * @note NapCat answers before it restarts its worker process, so its HTTP API and event
     * posts pause until the new process is up. Running in a single process or as its framework
     * build, it cannot restart itself and fails with `ErrorKind::Action`.
     */
    auto OneBot::SetRestart(
        SetRestartParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["delay"] = params.delay;
        return details::call_and_discard(m_transport, "set_restart", std::move(body), params.timeout);
    }

    /**
     * @brief Call `clean_cache`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing once the implementation cleaned its cache, or why the action failed.
     * @note NapCat deletes the files directly inside its temp directory and QQ's media and log
     * directories, not their subdirectories. It only logs the files it cannot delete, and fails
     * with `ErrorKind::Action` only when it cannot read its temp directory.
     */
    auto OneBot::CleanCache(
        CleanCacheParams params
    ) const -> drogon::Task<Result<void>> {
        return details::call_and_discard(m_transport, "clean_cache", Json::Value{ Json::objectValue }, params.timeout);
    }

} // namespace drogon_onebot
