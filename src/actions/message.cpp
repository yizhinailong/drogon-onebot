/**
 * @file message.cpp
 * @brief Implement the message actions of `OneBot`.
 */
#include <cstdint>
#include <string_view>
#include <utility>

#include "details/json.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {

    /**
     * @brief Read the sent message's ID from the result of a send action.
     * @param action Action name, for error messages.
     * @param data Result of `Transport::Call`.
     * @return The sent message, the call's error, or a `ResponseValidation` error when `data` has no `message_id`.
     */
    auto to_sent_message(
        std::string_view action,
        drogon_onebot::Result<Json::Value> const& data
    ) -> drogon_onebot::Result<drogon_onebot::SentMessage> {
        if (!data) {
            return std::unexpected{ data.error() };
        }
        auto const message_id{ drogon_onebot::details::read_integer<std::int32_t>(drogon_onebot::details::member(*data, "message_id")) };
        if (!message_id) {
            return std::unexpected{ drogon_onebot::details::invalid_data_error(action, "message_id", *data) };
        }
        return drogon_onebot::SentMessage{ .message_id = *message_id };
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `send_private_msg`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the sent message's ID, or why the action failed.
     */
    auto OneBot::SendPrivateMsg(
        SendPrivateMsgParams params
    ) const -> drogon::Task<Result<SentMessage>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["user_id"] = params.user_id;
        body["message"] = details::to_json(params.message);
        auto const data{ co_await transport->Call("send_private_msg", std::move(body), params.timeout) };
        co_return to_sent_message("send_private_msg", data);
    }

    /**
     * @brief Call `send_group_msg`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the sent message's ID, or why the action failed.
     */
    auto OneBot::SendGroupMsg(
        SendGroupMsgParams params
    ) const -> drogon::Task<Result<SentMessage>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["message"] = details::to_json(params.message);
        auto const data{ co_await transport->Call("send_group_msg", std::move(body), params.timeout) };
        co_return to_sent_message("send_group_msg", data);
    }

} // namespace drogon_onebot
