/**
 * @file request.cpp
 * @brief Implement the actions of `OneBot` that answer friend and group requests.
 */
#include <utility>

#include "details/transport.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot {

    /**
     * @brief Call `set_friend_add_request`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat fails with `ErrorKind::Action` when no pending request has `flag`.
     * Otherwise it does not wait for QQ's answer, so a refused request still succeeds.
     */
    auto OneBot::SetFriendAddRequest(
        SetFriendAddRequestParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["flag"] = params.flag;
        body["approve"] = params.approve;
        if (!params.remark.empty()) {
            body["remark"] = params.remark;
        }
        auto const data{ co_await transport->Call("set_friend_add_request", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

} // namespace drogon_onebot
