/**
 * @file group.cpp
 * @brief Implement the group actions of `OneBot`.
 */
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "details/action_call.hpp"
#include "details/field_reader.hpp"
#include "details/json.hpp"
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

    /**
     * @brief Read the `data` of `get_group_list`.
     * @param data Array to read.
     * @return The groups, or the first problem found, e.g. `data[1].group_name is missing`.
     */
    auto to_group_list(Json::Value const& data) -> std::expected<std::vector<drogon_onebot::GroupInfo>, std::string> {
        return drogon_onebot::details::read_array<drogon_onebot::GroupInfo>(data, "data", to_group_info);
    }

    /**
     * @brief Read a member object of the shape that `get_group_member_info` returns.
     * @param data Object to read; also an entry of the `get_group_member_list` result.
     * @return The member, or the first problem found, e.g. `user_id is missing`.
     */
    auto to_group_member_info(Json::Value const& data) -> std::expected<drogon_onebot::GroupMemberInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::GroupMemberInfo info{
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
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return info;
    }

    /**
     * @brief Read the `data` of `get_group_member_list`.
     * @param data Array to read.
     * @return The members, or the first problem found, e.g. `data[1].user_id is missing`.
     */
    auto to_group_member_list(Json::Value const& data) -> std::expected<std::vector<drogon_onebot::GroupMemberInfo>, std::string> {
        return drogon_onebot::details::read_array<drogon_onebot::GroupMemberInfo>(data, "data", to_group_member_info);
    }

    /**
     * @brief Read one honor holder of the `get_group_honor_info` result.
     * @param data Object to read: `current_talkative` or an entry of one of the lists.
     * @return The member, or the first problem found, e.g. `nickname is missing`.
     */
    auto to_group_honor_member(Json::Value const& data) -> std::expected<drogon_onebot::GroupHonorMember, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::GroupHonorMember member{
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .nickname = fields.RequiredString("nickname"),
            .avatar = fields.String("avatar"),
            .description = fields.String("description"),
            .day_count = fields.Integer<std::int32_t>("day_count").value_or(0),
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return member;
    }

    /**
     * @brief Read one honor list of the `get_group_honor_info` result.
     * @param data The result.
     * @param key Name of the list, e.g. `talkative_list`.
     * @return The members, none when the list is absent, or the first problem found.
     */
    auto to_group_honor_list(
        Json::Value const& data,
        char const* key
    ) -> std::expected<std::vector<drogon_onebot::GroupHonorMember>, std::string> {
        auto const& list{ drogon_onebot::details::member(data, key) };
        if (list.isNull()) {
            return std::vector<drogon_onebot::GroupHonorMember>{};
        }
        return drogon_onebot::details::read_array<drogon_onebot::GroupHonorMember>(list, key, to_group_honor_member);
    }

    /**
     * @brief Read the `data` of `get_group_honor_info`.
     * @param data Object to read.
     * @return The honors, or the first problem found, e.g. `current_talkative.nickname is missing`.
     */
    auto to_group_honor_info(Json::Value const& data) -> std::expected<drogon_onebot::GroupHonorInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::GroupHonorInfo info{ .group_id = fields.RequiredInteger<std::int64_t>("group_id"), .raw = data };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        // NapCat sends an empty object when nobody is the most talkative member.
        auto const& current{ drogon_onebot::details::member(data, "current_talkative") };
        if (!current.empty()) {
            auto talkative{ to_group_honor_member(current) };
            if (!talkative) {
                return std::unexpected{ std::format("current_talkative.{}", talkative.error()) };
            }
            info.current_talkative = std::move(*talkative);
        }
        for (auto const& [key, list] : {
                 std::pair{ "talkative_list", &info.talkative_list },
                 std::pair{ "performer_list", &info.performer_list },
                 std::pair{ "legend_list", &info.legend_list },
                 std::pair{ "strong_newbie_list", &info.strong_newbie_list },
                 std::pair{ "emotion_list", &info.emotion_list },
             }) {
            auto members{ to_group_honor_list(data, key) };
            if (!members) {
                return std::unexpected{ std::move(members.error()) };
            }
            *list = std::move(*members);
        }
        return info;
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `get_group_info`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the group, or why the action failed.
     */
    auto OneBot::GetGroupInfo(
        GetGroupInfoParams params
    ) const -> drogon::Task<Result<GroupInfo>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        return details::call_and_read<GroupInfo>(m_transport, "get_group_info", std::move(body), params.timeout, to_group_info);
    }

    /**
     * @brief Call `get_group_list`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the groups the bot account is in, or why the action failed.
     */
    auto OneBot::GetGroupList(
        GetGroupListParams params
    ) const -> drogon::Task<Result<std::vector<GroupInfo>>> {
        Json::Value body{ Json::objectValue };
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        return details::call_and_read<std::vector<GroupInfo>>(m_transport, "get_group_list", std::move(body), params.timeout, to_group_list);
    }

    /**
     * @brief Call `get_group_member_info`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the group member, or why the action failed.
     */
    auto OneBot::GetGroupMemberInfo(
        GetGroupMemberInfoParams params
    ) const -> drogon::Task<Result<GroupMemberInfo>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        return details::call_and_read<GroupMemberInfo>(m_transport, "get_group_member_info", std::move(body), params.timeout, to_group_member_info);
    }

    /**
     * @brief Call `get_group_member_list`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the group's members, or why the action failed.
     */
    auto OneBot::GetGroupMemberList(
        GetGroupMemberListParams params
    ) const -> drogon::Task<Result<std::vector<GroupMemberInfo>>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        if (params.no_cache) {
            body["no_cache"] = *params.no_cache;
        }
        return details::call_and_read<std::vector<GroupMemberInfo>>(m_transport, "get_group_member_list", std::move(body), params.timeout, to_group_member_list);
    }

    /**
     * @brief Call `get_group_honor_info`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the group's honors, or why the action failed.
     * @note NapCat reads the honors from QQ's web page and leaves a list empty when that
     * fails, so an empty list does not prove that nobody holds the honor.
     */
    auto OneBot::GetGroupHonorInfo(
        GetGroupHonorInfoParams params
    ) const -> drogon::Task<Result<GroupHonorInfo>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["type"] = params.type;
        return details::call_and_read<GroupHonorInfo>(m_transport, "get_group_honor_info", std::move(body), params.timeout, to_group_honor_info);
    }

    /**
     * @brief Call `set_group_kick`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat does not check QQ's answer, so a removal that QQ refuses, e.g.
     * without admin rights, still succeeds.
     */
    auto OneBot::SetGroupKick(
        SetGroupKickParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["reject_add_request"] = params.reject_add_request;
        return details::call_and_discard(m_transport, "set_group_kick", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_ban`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat fails with `ErrorKind::Action` for a member not in the group, for the
     * group owner, and for an admin unless the bot account owns the group.
     */
    auto OneBot::SetGroupBan(
        SetGroupBanParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["duration"] = params.duration;
        return details::call_and_discard(m_transport, "set_group_ban", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_whole_ban`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat reports QQ's refusal, e.g. without admin rights, as `ErrorKind::Action`.
     */
    auto OneBot::SetGroupWholeBan(
        SetGroupWholeBanParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["enable"] = params.enable;
        return details::call_and_discard(m_transport, "set_group_whole_ban", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_admin`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat does not check QQ's answer, so a change that QQ refuses, e.g. when
     * the bot account does not own the group, still succeeds.
     */
    auto OneBot::SetGroupAdmin(
        SetGroupAdminParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["enable"] = params.enable;
        return details::call_and_discard(m_transport, "set_group_admin", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_card`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat reads the card back for about 3 seconds after setting it, and fails
     * with `ErrorKind::Action` when it has not changed by then.
     */
    auto OneBot::SetGroupCard(
        SetGroupCardParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["card"] = params.card;
        return details::call_and_discard(m_transport, "set_group_card", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_name`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat reports QQ's refusal as `ErrorKind::Action`.
     */
    auto OneBot::SetGroupName(
        SetGroupNameParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["group_name"] = params.group_name;
        return details::call_and_discard(m_transport, "set_group_name", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_leave`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat does not check QQ's answer, so a request that QQ refuses still succeeds.
     */
    auto OneBot::SetGroupLeave(
        SetGroupLeaveParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["is_dismiss"] = params.is_dismiss;
        return details::call_and_discard(m_transport, "set_group_leave", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_special_title`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat sets titles through its packet backend and fails with
     * `ErrorKind::Action` (retcode 400) when that is unavailable.
     */
    auto OneBot::SetGroupSpecialTitle(
        SetGroupSpecialTitleParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["user_id"] = params.user_id;
        body["special_title"] = params.special_title;
        body["duration"] = params.duration;
        return details::call_and_discard(m_transport, "set_group_special_title", std::move(body), params.timeout);
    }

} // namespace drogon_onebot
