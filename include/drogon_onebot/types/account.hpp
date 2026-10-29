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

} // namespace drogon_onebot
