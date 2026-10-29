/**
 * @file credential.cpp
 * @brief Implement the credential actions of `OneBot`.
 */
#include <cstdint>
#include <expected>
#include <utility>

#include "details/field_reader.hpp"
#include "details/json.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot {

    /**
     * @brief Call `get_cookies`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the bot account's cookies, or why the action failed.
     * @note NapCat also answers with the cookies' CSRF token as the string `bkn`, which
     * `Cookies::raw` keeps.
     */
    auto OneBot::GetCookies(
        GetCookiesParams params
    ) const -> drogon::Task<Result<Cookies>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        // NapCat fails the call with retcode 400 when domain is missing, even though the standard defaults it.
        body["domain"] = params.domain;
        auto const data{ co_await transport->Call("get_cookies", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        Cookies cookies{
            .cookies = fields.RequiredString("cookies"),
            .raw = *data,
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_cookies", fields.Problem(), *data) };
        }
        co_return cookies;
    }

    /**
     * @brief Call `get_csrf_token`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the bot account's CSRF token, or why the action failed.
     * @note NapCat fails with `ErrorKind::Action` when it cannot get the account's `skey` from QQ.
     */
    auto OneBot::GetCsrfToken(
        GetCsrfTokenParams params
    ) const -> drogon::Task<Result<CsrfToken>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("get_csrf_token", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        CsrfToken token{
            .token = fields.RequiredInteger<std::int32_t>("token"),
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_csrf_token", fields.Problem(), *data) };
        }
        co_return token;
    }

    /**
     * @brief Call `get_credentials`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the bot account's cookies and CSRF token, or why the action failed.
     * @note NapCat sends the token as `token`, like `get_csrf_token`, instead of the
     * standard's `csrf_token`; this reads either.
     */
    auto OneBot::GetCredentials(
        GetCredentialsParams params
    ) const -> drogon::Task<Result<Credentials>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        // NapCat fails the call with retcode 400 when domain is missing, even though the standard defaults it.
        body["domain"] = params.domain;
        auto const data{ co_await transport->Call("get_credentials", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        char const* const token_key{ details::member(*data, "token").isNull() ? "csrf_token" : "token" };
        Credentials credentials{
            .cookies = fields.RequiredString("cookies"),
            .csrf_token = fields.RequiredInteger<std::int32_t>(token_key),
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_credentials", fields.Problem(), *data) };
        }
        co_return credentials;
    }

} // namespace drogon_onebot
