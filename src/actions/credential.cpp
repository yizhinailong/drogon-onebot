/**
 * @file credential.cpp
 * @brief Implement the credential actions of `OneBot`.
 */
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

#include "details/action_call.hpp"
#include "details/field_reader.hpp"
#include "details/json.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {

    /**
     * @brief Read the `data` of `get_cookies`.
     * @param data Object to read.
     * @return The cookies, or the first problem found, e.g. `cookies is missing`.
     */
    auto to_cookies(Json::Value const& data) -> std::expected<drogon_onebot::Cookies, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::Cookies cookies{
            .cookies = fields.RequiredString("cookies"),
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return cookies;
    }

    /**
     * @brief Read the `data` of `get_csrf_token`.
     * @param data Object to read.
     * @return The token, or the first problem found, e.g. `token is missing`.
     */
    auto to_csrf_token(Json::Value const& data) -> std::expected<drogon_onebot::CsrfToken, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::CsrfToken token{
            .token = fields.RequiredInteger<std::int32_t>("token"),
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return token;
    }

    /**
     * @brief Read the `data` of `get_credentials`, with the token as NapCat's `token` or the standard's `csrf_token`.
     * @param data Object to read.
     * @return The credentials, or the first problem found, e.g. `csrf_token is missing`.
     */
    auto to_credentials(Json::Value const& data) -> std::expected<drogon_onebot::Credentials, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        char const* const token_key{ drogon_onebot::details::member(data, "token").isNull() ? "csrf_token" : "token" };
        drogon_onebot::Credentials credentials{
            .cookies = fields.RequiredString("cookies"),
            .csrf_token = fields.RequiredInteger<std::int32_t>(token_key),
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return credentials;
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `get_cookies`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the bot account's cookies, or why the action failed.
     * @note NapCat also answers with the cookies' CSRF token as the string `bkn`, which
     * `Cookies::raw` keeps.
     */
    auto OneBot::GetCookies(
        GetCookiesParams params
    ) const -> drogon::Task<Result<Cookies>> {
        Json::Value body{ Json::objectValue };
        // NapCat fails the call with retcode 400 when domain is missing, even though the standard defaults it.
        body["domain"] = params.domain;
        return details::call_and_read<Cookies>(m_transport, "get_cookies", std::move(body), params.timeout, to_cookies);
    }

    /**
     * @brief Call `get_csrf_token`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the bot account's CSRF token, or why the action failed.
     * @note NapCat fails with `ErrorKind::Action` when it cannot get the account's `skey` from QQ.
     */
    auto OneBot::GetCsrfToken(
        GetCsrfTokenParams params
    ) const -> drogon::Task<Result<CsrfToken>> {
        return details::call_and_read<CsrfToken>(m_transport, "get_csrf_token", Json::Value{ Json::objectValue }, params.timeout, to_csrf_token);
    }

    /**
     * @brief Call `get_credentials`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the bot account's cookies and CSRF token, or why the action failed.
     * @note NapCat sends the token as `token`, like `get_csrf_token`, instead of the
     * standard's `csrf_token`; this reads either.
     */
    auto OneBot::GetCredentials(
        GetCredentialsParams params
    ) const -> drogon::Task<Result<Credentials>> {
        Json::Value body{ Json::objectValue };
        // NapCat fails the call with retcode 400 when domain is missing, even though the standard defaults it.
        body["domain"] = params.domain;
        return details::call_and_read<Credentials>(m_transport, "get_credentials", std::move(body), params.timeout, to_credentials);
    }

} // namespace drogon_onebot
