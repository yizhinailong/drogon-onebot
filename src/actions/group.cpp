/**
 * @file group.cpp
 * @brief Implement the group actions of `OneBot`.
 */
#include <cstdint>
#include <utility>

#include "details/field_reader.hpp"
#include "details/json.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot {

    /**
     * @brief Call `get_group_info`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the group, or why the action failed.
     */
    auto OneBot::GetGroupInfo(
        GetGroupInfoParams params
    ) const -> drogon::Task<Result<GroupInfo>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        auto const data{ co_await transport->Call("get_group_info", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        GroupInfo info{
            .group_id = fields.RequiredInteger<std::int64_t>("group_id"),
            .group_name = fields.RequiredString("group_name"),
            .member_count = fields.Integer<std::int32_t>("member_count").value_or(0),
            .max_member_count = fields.Integer<std::int32_t>("max_member_count").value_or(0),
            .raw = *data,
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_group_info", fields.Problem(), *data) };
        }
        co_return info;
    }

    /**
     * @brief Call `get_group_member_info`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the group member, or why the action failed.
     */
    auto OneBot::GetGroupMemberInfo(
        GetGroupMemberInfoParams params
    ) const -> drogon::Task<Result<GroupMemberInfo>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        auto const data{ co_await transport->Call("get_group_member_info", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        GroupMemberInfo info{
            .group_id = fields.RequiredInteger<std::int64_t>("group_id"),
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .nickname = fields.RequiredString("nickname"),
            .card = fields.String("card"),
            .sex = fields.String("sex"),
            .age = fields.Integer<std::int32_t>("age").value_or(0),
            .area = fields.String("area"),
            .join_time = fields.Integer<std::int64_t>("join_time").value_or(0),
            .last_sent_time = fields.Integer<std::int64_t>("last_sent_time").value_or(0),
            .level = fields.String("level"),
            .role = fields.String("role"),
            .unfriendly = fields.Bool("unfriendly"),
            .title = fields.String("title"),
            .title_expire_time = fields.Integer<std::int64_t>("title_expire_time").value_or(0),
            .card_changeable = fields.Bool("card_changeable"),
            .raw = *data,
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_group_member_info", fields.Problem(), *data) };
        }
        co_return info;
    }

    /**
     * @brief Call `set_group_kick`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat does not check QQ's answer, so a removal that QQ refuses, e.g.
     * without admin rights, still succeeds.
     */
    auto OneBot::SetGroupKick(
        SetGroupKickParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["reject_add_request"] = params.reject_add_request;
        auto const data{ co_await transport->Call("set_group_kick", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

} // namespace drogon_onebot
