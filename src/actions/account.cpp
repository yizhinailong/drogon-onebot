/**
 * @file account.cpp
 * @brief Implement the account actions of `OneBot`.
 */
#include "details/json.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot {

    /**
     * @brief Call `get_login_info`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the logged-in account, or why the action failed.
     */
    auto OneBot::GetLoginInfo(
        GetLoginInfoParams params
    ) const -> drogon::Task<Result<LoginInfo>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        auto const data{ co_await transport->Call("get_login_info", Json::Value{ Json::objectValue }, params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        auto const user_id{ details::read_integer<std::int64_t>(details::member(*data, "user_id")) };
        auto const& nickname{ details::member(*data, "nickname") };
        if (!user_id) {
            co_return std::unexpected{ details::invalid_data_error("get_login_info", "user_id", *data) };
        }
        if (!nickname.isString()) {
            co_return std::unexpected{ details::invalid_data_error("get_login_info", "nickname", *data) };
        }
        co_return LoginInfo{
            .user_id = *user_id,
            .nickname = nickname.asString(),
        };
    }

} // namespace drogon_onebot
