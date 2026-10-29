/**
 * @file account.cpp
 * @brief Implement the account actions of `OneBot`.
 */
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "details/field_reader.hpp"
#include "details/json.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {

    /**
     * @brief Read one entry of the `get_friend_list` result.
     * @param entry Object to read.
     * @return The friend, or the first problem found, e.g. `user_id is missing`.
     */
    auto to_friend_info(Json::Value const& entry) -> std::expected<drogon_onebot::FriendInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ entry };
        drogon_onebot::FriendInfo info{
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .nickname = fields.RequiredString("nickname"),
            .remark = fields.String("remark"),
            .raw = entry,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return info;
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `get_login_info`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the logged-in account, or why the action failed.
     */
    auto OneBot::GetLoginInfo(
        GetLoginInfoParams params
    ) const -> drogon::Task<Result<LoginInfo>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("get_login_info", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        LoginInfo info{
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .nickname = fields.RequiredString("nickname"),
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_login_info", fields.Problem(), *data) };
        }
        co_return info;
    }

    /**
     * @brief Call `get_stranger_info`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the account's public profile, or why the action failed.
     */
    auto OneBot::GetStrangerInfo(
        GetStrangerInfoParams params
    ) const -> drogon::Task<Result<StrangerInfo>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["user_id"] = params.user_id;
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        auto const data{ co_await transport->Call("get_stranger_info", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        StrangerInfo info{
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .nickname = fields.RequiredString("nickname"),
            .sex = fields.String("sex"),
            .age = fields.Integer<std::int32_t>("age").value_or(0),
            .raw = *data,
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_stranger_info", fields.Problem(), *data) };
        }
        co_return info;
    }

    /**
     * @brief Call `send_like`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing once the likes are sent, or why the action failed.
     * @note NapCat reports a like that QQ refuses as `ErrorKind::Action`.
     */
    auto OneBot::SendLike(
        SendLikeParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["user_id"] = params.user_id;
        body["times"] = params.times;
        auto const data{ co_await transport->Call("send_like", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `get_friend_list`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the bot account's friends, or why the action failed.
     */
    auto OneBot::GetFriendList(
        GetFriendListParams params
    ) const -> drogon::Task<Result<std::vector<FriendInfo>>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        auto const data{ co_await transport->Call("get_friend_list", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        auto friends{ details::read_array<FriendInfo>(*data, "data", to_friend_info) };
        if (!friends) {
            co_return std::unexpected{ details::invalid_data_error("get_friend_list", friends.error(), *data) };
        }
        co_return std::move(*friends);
    }

} // namespace drogon_onebot
