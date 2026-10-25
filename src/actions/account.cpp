/**
 * @file account.cpp
 * @brief Implement the account actions of `OneBot`.
 */
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot {

    /**
     * @brief Call `get_login_info`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the logged-in account, or why the action failed.
     */
    auto OneBot::GetLoginInfo(
        [[maybe_unused]] GetLoginInfoParams params
    ) const -> drogon::Task<Result<LoginInfo>> {
        // TODO: Copy m_transport into the frame, call `get_login_info`, and read `user_id` and `nickname` from `data`.
        co_return std::unexpected{ Error{
            .kind = ErrorKind::Action,
            .message = "get_login_info: not implemented",
        } };
    }

} // namespace drogon_onebot
