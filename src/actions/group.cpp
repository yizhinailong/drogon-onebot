/**
 * @file group.cpp
 * @brief Implement the group actions of `OneBot`.
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
     * @brief Read a group object of the shape that `get_group_info` returns.
     * @param data Object to read; also an entry of the `get_group_list` result.
     * @return The group, or the first problem found, e.g. `group_name is missing`.
     */
    auto to_group_info(Json::Value const& data) -> std::expected<drogon_onebot::GroupInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::GroupInfo info{
            .group_id = fields.RequiredInteger<std::int64_t>("group_id"),
            .group_name = fields.RequiredString("group_name"),
            .member_count = fields.Integer<std::int32_t>("member_count").value_or(0),
            .max_member_count = fields.Integer<std::int32_t>("max_member_count").value_or(0),
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
        auto info{ to_group_info(*data) };
        if (!info) {
            co_return std::unexpected{ details::invalid_data_error("get_group_info", info.error(), *data) };
        }
        co_return std::move(*info);
    }

    /**
     * @brief Call `get_group_list`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the groups the bot account is in, or why the action failed.
     */
    auto OneBot::GetGroupList(
        GetGroupListParams params
    ) const -> drogon::Task<Result<std::vector<GroupInfo>>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        auto const data{ co_await transport->Call("get_group_list", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        auto groups{ details::read_array<GroupInfo>(*data, "data", to_group_info) };
        if (!groups) {
            co_return std::unexpected{ details::invalid_data_error("get_group_list", groups.error(), *data) };
        }
        co_return std::move(*groups);
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

    /**
     * @brief Call `set_group_ban`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat fails with `ErrorKind::Action` for a member not in the group, for the
     * group owner, and for an admin unless the bot account owns the group.
     */
    auto OneBot::SetGroupBan(
        SetGroupBanParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["duration"] = params.duration;
        auto const data{ co_await transport->Call("set_group_ban", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `set_group_whole_ban`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat reports QQ's refusal, e.g. without admin rights, as `ErrorKind::Action`.
     */
    auto OneBot::SetGroupWholeBan(
        SetGroupWholeBanParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["enable"] = params.enable;
        auto const data{ co_await transport->Call("set_group_whole_ban", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `set_group_admin`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat does not check QQ's answer, so a change that QQ refuses, e.g. when
     * the bot account does not own the group, still succeeds.
     */
    auto OneBot::SetGroupAdmin(
        SetGroupAdminParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["enable"] = params.enable;
        auto const data{ co_await transport->Call("set_group_admin", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `set_group_card`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat reads the card back for about 3 seconds after setting it, and fails
     * with `ErrorKind::Action` when it has not changed by then.
     */
    auto OneBot::SetGroupCard(
        SetGroupCardParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["card"] = params.card;
        auto const data{ co_await transport->Call("set_group_card", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `set_group_name`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat reports QQ's refusal as `ErrorKind::Action`.
     */
    auto OneBot::SetGroupName(
        SetGroupNameParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["group_name"] = params.group_name;
        auto const data{ co_await transport->Call("set_group_name", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `set_group_leave`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat does not check QQ's answer, so a request that QQ refuses still succeeds.
     */
    auto OneBot::SetGroupLeave(
        SetGroupLeaveParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["is_dismiss"] = params.is_dismiss;
        auto const data{ co_await transport->Call("set_group_leave", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `set_group_special_title`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat sets titles through its packet backend and fails with
     * `ErrorKind::Action` (retcode 400) when that is unavailable.
     */
    auto OneBot::SetGroupSpecialTitle(
        SetGroupSpecialTitleParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["special_title"] = params.special_title;
        body["duration"] = params.duration;
        auto const data{ co_await transport->Call("set_group_special_title", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

} // namespace drogon_onebot
