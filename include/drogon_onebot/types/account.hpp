/**
 * @file account.hpp
 * @brief Declare the request and response types of the account actions.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::GetLoginInfo()`.
     */
    struct GetLoginInfoParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Account the OneBot implementation is logged in as, from `get_login_info`.
     */
    struct LoginInfo {
        std::int64_t user_id{}; ///< QQ number of the logged-in account.
        std::string nickname;   ///< Nickname of the logged-in account.
    };

    /**
     * @brief Arguments of `OneBot::GetStrangerInfo()`.
     */
    struct GetStrangerInfoParams {
        std::int64_t user_id{};       ///< QQ number of the account.
        std::optional<bool> no_cache; ///< Whether to skip the implementation's cache; empty leaves it to the implementation.
        double timeout{ 30.0 };       ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Public profile of any QQ account, from `get_stranger_info`.
     */
    struct StrangerInfo {
        std::int64_t user_id{}; ///< QQ number of the account.
        std::string nickname;   ///< Nickname of the account.
        std::string sex;        ///< `male`, `female`, or `unknown`; empty when absent.
        std::int32_t age{};     ///< Age; 0 when absent.
        Json::Value raw;        ///< Whole `data` as received, for fields without a member here, e.g. NapCat's `qid` and `long_nick`.
    };

    /**
     * @brief Arguments of `OneBot::GetFriendList()`.
     */
    struct GetFriendListParams {
        std::optional<bool> no_cache; ///< Whether to skip the implementation's cache, a NapCat extension; empty sends none.
        double timeout{ 30.0 };       ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Friend of the bot account, from `get_friend_list`.
     */
    struct FriendInfo {
        std::int64_t user_id{}; ///< QQ number of the friend.
        std::string nickname;   ///< Nickname of the friend.
        std::string remark;     ///< Remark the bot account gave the friend; empty when absent.
        Json::Value raw;        ///< Whole entry as received, for fields without a member here, e.g. NapCat's `sex` and `level`.
    };

    /**
     * @brief Arguments of `OneBot::SendLike()`.
     */
    struct SendLikeParams {
        std::int64_t user_id{};  ///< QQ number of the account to like.
        std::int32_t times{ 1 }; ///< Number of likes; the standard allows 10 per friend a day.
        double timeout{ 30.0 };  ///< Seconds to wait for the response; 0 waits forever.
    };

} // namespace drogon_onebot
