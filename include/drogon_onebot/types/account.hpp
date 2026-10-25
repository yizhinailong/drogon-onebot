/**
 * @file account.hpp
 * @brief Declare the request and response types of the account actions.
 */
#pragma once

#include <cstdint>
#include <string>

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

} // namespace drogon_onebot
