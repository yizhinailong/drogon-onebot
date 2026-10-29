/**
 * @file group.hpp
 * @brief Declare the request and response types of the group actions.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::GetGroupInfo()`.
     */
    struct GetGroupInfoParams {
        std::int64_t group_id{};      ///< Group number.
        std::optional<bool> no_cache; ///< Whether to skip the implementation's cache; empty leaves it to the implementation.
        double timeout{ 30.0 };       ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Group the bot account is in, from `get_group_info` or `get_group_list`.
     */
    struct GroupInfo {
        std::int64_t group_id{};         ///< Group number.
        std::string group_name;          ///< Group name.
        std::int32_t member_count{};     ///< Current number of members; 0 when absent.
        std::int32_t max_member_count{}; ///< Largest number of members the group allows; 0 when absent.
        Json::Value raw;                 ///< Whole `data`, or list entry, as received, for fields without a member here, e.g. NapCat's `group_remark`.
    };

    /**
     * @brief Arguments of `OneBot::GetGroupList()`.
     */
    struct GetGroupListParams {
        std::optional<bool> no_cache; ///< Whether to skip the implementation's cache, a NapCat extension; empty sends none.
        double timeout{ 30.0 };       ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::GetGroupMemberInfo()`.
     */
    struct GetGroupMemberInfoParams {
        std::int64_t group_id{};      ///< Group number.
        std::int64_t user_id{};       ///< QQ number of the member.
        std::optional<bool> no_cache; ///< Whether to skip the implementation's cache; empty leaves it to the implementation (NapCat then skips it).
        double timeout{ 30.0 };       ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Member of a group, from `get_group_member_info`.
     *
     * Members other than the IDs and `nickname` read as empty, 0, or false when the
     * implementation leaves them out.
     */
    struct GroupMemberInfo {
        std::int64_t group_id{};          ///< Group number.
        std::int64_t user_id{};           ///< QQ number of the member.
        std::string nickname;             ///< QQ nickname of the member, not the group card.
        std::string card;                 ///< Group card; empty when unset.
        std::string sex;                  ///< `male`, `female`, or `unknown`.
        std::int32_t age{};               ///< Age.
        std::string area;                 ///< Region.
        std::int64_t join_time{};         ///< Unix time in seconds when the member joined.
        std::int64_t last_sent_time{};    ///< Unix time in seconds of the member's last message.
        std::string level;                ///< Member level in the group, as a decimal string.
        std::string role;                 ///< `owner`, `admin`, or `member`; NapCat leaves it out for some members.
        bool unfriendly{};                ///< Whether the member has a record of misbehavior.
        std::string title;                ///< Special title.
        std::int64_t title_expire_time{}; ///< Unix time in seconds when the special title expires.
        bool card_changeable{};           ///< Whether the bot account may change the member's card.
        Json::Value raw;                  ///< Whole `data` as received, for fields without a member here, e.g. NapCat's `is_robot`.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupKick()`.
     */
    struct SetGroupKickParams {
        std::int64_t group_id{};          ///< Group number.
        std::int64_t user_id{};           ///< QQ number of the member to remove.
        bool reject_add_request{ false }; ///< Whether to refuse the member's later requests to join.
        double timeout{ 30.0 };           ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupBan()`.
     */
    struct SetGroupBanParams {
        std::int64_t group_id{};       ///< Group number.
        std::int64_t user_id{};        ///< QQ number of the member to mute.
        std::int32_t duration{ 1800 }; ///< Seconds to mute the member, 30 minutes by default as in the standard; 0 lifts the mute.
        double timeout{ 30.0 };        ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupWholeBan()`.
     */
    struct SetGroupWholeBanParams {
        std::int64_t group_id{}; ///< Group number.
        bool enable{ true };     ///< Whether to mute every member; false lifts the mute.
        double timeout{ 30.0 };  ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupAdmin()`.
     */
    struct SetGroupAdminParams {
        std::int64_t group_id{}; ///< Group number.
        std::int64_t user_id{};  ///< QQ number of the member.
        bool enable{ true };     ///< Whether to make the member an admin; false removes the role.
        double timeout{ 30.0 };  ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupCard()`.
     */
    struct SetGroupCardParams {
        std::int64_t group_id{}; ///< Group number.
        std::int64_t user_id{};  ///< QQ number of the member.
        std::string card;        ///< New group card; empty removes it.
        double timeout{ 30.0 };  ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupName()`.
     */
    struct SetGroupNameParams {
        std::int64_t group_id{}; ///< Group number.
        std::string group_name;  ///< New group name.
        double timeout{ 30.0 };  ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupLeave()`.
     */
    struct SetGroupLeaveParams {
        std::int64_t group_id{};  ///< Group number.
        bool is_dismiss{ false }; ///< Whether to disband the group instead of leaving it, which only its owner can do.
        double timeout{ 30.0 };   ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SetGroupSpecialTitle()`.
     */
    struct SetGroupSpecialTitleParams {
        std::int64_t group_id{};     ///< Group number.
        std::int64_t user_id{};      ///< QQ number of the member.
        std::string special_title;   ///< New special title; empty removes it.
        std::int64_t duration{ -1 }; ///< Seconds the title lasts, -1 for ever as in the standard; NapCat ignores it.
        double timeout{ 30.0 };      ///< Seconds to wait for the response; 0 waits forever.
    };

} // namespace drogon_onebot
