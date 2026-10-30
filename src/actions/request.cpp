/**
 * @file request.cpp
 * @brief Implement the actions of `OneBot` that answer friend and group requests.
 */
#include <utility>

#include "details/action_call.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot {

    /**
     * @brief Call `set_friend_add_request`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat fails with `ErrorKind::Action` when no pending request has `flag`.
     * Otherwise it does not wait for QQ's answer, so a refused request still succeeds.
     */
    auto OneBot::SetFriendAddRequest(
        SetFriendAddRequestParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["flag"] = params.flag;
        body["approve"] = params.approve;
        if (!params.remark.empty()) {
            body["remark"] = params.remark;
        }
        return details::call_and_discard(m_transport, "set_friend_add_request", std::move(body), params.timeout);
    }

    /**
     * @brief Call `set_group_add_request`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing on success, or why the action failed.
     * @note NapCat fails with `ErrorKind::Action` when it finds no request with `flag`
     * among the bot's invitations and its latest group notifications. Otherwise it does
     * not check QQ's answer, so a refused request still succeeds.
     */
    auto OneBot::SetGroupAddRequest(
        SetGroupAddRequestParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["flag"] = params.flag;
        if (!params.sub_type.empty()) {
            body["sub_type"] = params.sub_type;
        }
        body["approve"] = params.approve;
        // NapCat sends an empty reason to QQ as is, which can fail; without one it sends a space.
        if (!params.reason.empty()) {
            body["reason"] = params.reason;
        }
        return details::call_and_discard(m_transport, "set_group_add_request", std::move(body), params.timeout);
    }

} // namespace drogon_onebot
