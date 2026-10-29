/**
 * @file credential.hpp
 * @brief Declare the request and response types of the credential actions.
 */
#pragma once

#include <string>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::GetCookies()`.
     */
    struct GetCookiesParams {
        std::string domain;     ///< Domain to get the cookies for, e.g. `qun.qq.com`; empty by default, as in the standard. Always sent, because NapCat requires it.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Cookies of the bot account, from `get_cookies`.
     */
    struct Cookies {
        std::string cookies; ///< Cookies in `Cookie` header form, e.g. `uin=o10001; skey=@abc12345`.
        Json::Value raw;     ///< Whole `data` as received, for fields without a member here, e.g. NapCat's `bkn`.
    };

} // namespace drogon_onebot
