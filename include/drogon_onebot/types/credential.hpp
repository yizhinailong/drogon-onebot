/**
 * @file credential.hpp
 * @brief Declare the request and response types of the credential actions.
 */
#pragma once

#include <cstdint>
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

    /**
     * @brief Arguments of `OneBot::GetCsrfToken()`.
     */
    struct GetCsrfTokenParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief CSRF token of the bot account, from `get_csrf_token`.
     */
    struct CsrfToken {
        std::int32_t token{}; ///< CSRF token that QQ's web APIs take, e.g. as `bkn` or `g_tk`.
    };

    /**
     * @brief Arguments of `OneBot::GetCredentials()`.
     */
    struct GetCredentialsParams {
        std::string domain;     ///< Domain to get the cookies for, e.g. `qun.qq.com`; empty by default, as in the standard. Always sent, because NapCat requires it.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Cookies and CSRF token of the bot account, from `get_credentials`.
     */
    struct Credentials {
        std::string cookies;       ///< Cookies in `Cookie` header form, as from `get_cookies`.
        std::int32_t csrf_token{}; ///< CSRF token computed from the cookies' `skey`; NapCat sends 0 when they have none.
    };

} // namespace drogon_onebot
