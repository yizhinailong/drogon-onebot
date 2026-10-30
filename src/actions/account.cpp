/**
 * @file account.cpp
 * @brief Implement the account actions of `OneBot`.
 */
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "details/action_call.hpp"
#include "details/field_reader.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {

    /**
     * @brief Read the `data` of `get_login_info`.
     * @param data Object to read.
     * @return The account, or the first problem found, e.g. `user_id is missing`.
     */
    auto to_login_info(Json::Value const& data) -> std::expected<drogon_onebot::LoginInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::LoginInfo info{
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .nickname = fields.RequiredString("nickname"),
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return info;
    }

    /**
     * @brief Read the `data` of `get_stranger_info`.
     * @param data Object to read.
     * @return The profile, or the first problem found, e.g. `nickname is missing`.
     */
    auto to_stranger_info(Json::Value const& data) -> std::expected<drogon_onebot::StrangerInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::StrangerInfo info{
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .nickname = fields.RequiredString("nickname"),
            .sex = fields.String("sex"),
            .age = fields.Integer<std::int32_t>("age").value_or(0),
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return info;
    }

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

    /**
     * @brief Read the `data` of `get_friend_list`.
     * @param data Array to read.
     * @return The friends, or the first problem found, e.g. `data[1].nickname is missing`.
     */
    auto to_friend_list(Json::Value const& data) -> std::expected<std::vector<drogon_onebot::FriendInfo>, std::string> {
        return drogon_onebot::details::read_array<drogon_onebot::FriendInfo>(data, "data", to_friend_info);
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `get_login_info`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the logged-in account, or why the action failed.
     */
    auto OneBot::GetLoginInfo(
        GetLoginInfoParams params
    ) const -> drogon::Task<Result<LoginInfo>> {
        return details::call_and_read<LoginInfo>(m_transport, "get_login_info", Json::Value{ Json::objectValue }, params.timeout, to_login_info);
    }

    /**
     * @brief Call `get_stranger_info`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the account's public profile, or why the action failed.
     */
    auto OneBot::GetStrangerInfo(
        GetStrangerInfoParams params
    ) const -> drogon::Task<Result<StrangerInfo>> {
        Json::Value body{ Json::objectValue };
        body["user_id"] = params.user_id;
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        return details::call_and_read<StrangerInfo>(m_transport, "get_stranger_info", std::move(body), params.timeout, to_stranger_info);
    }

    /**
     * @brief Call `send_like`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing once the likes are sent, or why the action failed.
     * @note NapCat reports a like that QQ refuses as `ErrorKind::Action`.
     */
    auto OneBot::SendLike(
        SendLikeParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["user_id"] = params.user_id;
        body["times"] = params.times;
        return details::call_and_discard(m_transport, "send_like", std::move(body), params.timeout);
    }

    /**
     * @brief Call `get_friend_list`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the bot account's friends, or why the action failed.
     */
    auto OneBot::GetFriendList(
        GetFriendListParams params
    ) const -> drogon::Task<Result<std::vector<FriendInfo>>> {
        Json::Value body{ Json::objectValue };
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        return details::call_and_read<std::vector<FriendInfo>>(m_transport, "get_friend_list", std::move(body), params.timeout, to_friend_list);
    }

} // namespace drogon_onebot
