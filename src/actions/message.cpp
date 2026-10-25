/**
 * @file message.cpp
 * @brief Implement the message actions of `OneBot`.
 */
#include "drogon_onebot/onebot_client.hpp"

namespace drogon_onebot {

    /**
     * @brief Call `send_private_msg`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the sent message's ID, or why the action failed.
     */
    auto OneBot::SendPrivateMsg(
        [[maybe_unused]] SendPrivateMsgParams params
    ) const -> drogon::Task<Result<SentMessage>> {
        // TODO: Copy m_transport into the frame, send `user_id` and `message` as a segment array, and read `message_id` from `data`.
        co_return std::unexpected{ Error{
            .kind = ErrorKind::Action,
            .message = "send_private_msg: not implemented",
        } };
    }

    /**
     * @brief Call `send_group_msg`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the sent message's ID, or why the action failed.
     */
    auto OneBot::SendGroupMsg(
        [[maybe_unused]] SendGroupMsgParams params
    ) const -> drogon::Task<Result<SentMessage>> {
        // TODO: Copy m_transport into the frame, send `group_id` and `message` as a segment array, and read `message_id` from `data`.
        co_return std::unexpected{ Error{
            .kind = ErrorKind::Action,
            .message = "send_group_msg: not implemented",
        } };
    }

} // namespace drogon_onebot
