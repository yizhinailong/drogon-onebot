/**
 * @file message.cpp
 * @brief Implement the message actions of `OneBot`.
 */
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

#include "details/field_reader.hpp"
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
        drogon_onebot::details::FieldReader fields{ *data };
        drogon_onebot::SentMessage const sent{ .message_id = fields.RequiredInteger<std::int32_t>("message_id") };
        if (!fields.Problem().empty()) {
            return std::unexpected{ drogon_onebot::details::invalid_data_error(action, fields.Problem(), *data) };
        }
        return sent;
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

    /**
     * @brief Call `delete_msg`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding nothing once the message is recalled, or why the action failed.
     * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
     * an older ID fails with `ErrorKind::Action`.
     */
    auto OneBot::DeleteMsg(
        DeleteMsgParams params
    ) const -> drogon::Task<Result<void>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["message_id"] = params.message_id;
        auto const data{ co_await transport->Call("delete_msg", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

    /**
     * @brief Call `get_msg`.
     * @param params Request fields, taken by value so they outlive the caller's frame.
     * @return Task yielding the message, or why the action failed.
     * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
     * an older ID fails with `ErrorKind::Action`.
     */
    auto OneBot::GetMsg(
        GetMsgParams params
    ) const -> drogon::Task<Result<MessageInfo>> {
        // Keep the transport in the coroutine frame in case this object dies mid-request.
        auto const transport{ m_transport };
        Json::Value body{ Json::objectValue };
        body["message_id"] = params.message_id;
        auto const data{ co_await transport->Call("get_msg", std::move(body), params.timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        details::FieldReader fields{ *data };
        details::FieldReader sender{ details::member(*data, "sender") };
        MessageInfo info{
            .time = fields.Integer<std::int64_t>("time").value_or(0),
            .message_type = fields.RequiredString("message_type"),
            .message_id = fields.RequiredInteger<std::int32_t>("message_id"),
            .real_id = fields.Integer<std::int32_t>("real_id").value_or(0),
            .group_id = fields.Integer<std::int64_t>("group_id"),
            .sender = Sender{
                .user_id = sender.RequiredInteger<std::int64_t>("user_id"),
                .nickname = sender.String("nickname"),
                .card = sender.String("card"),
                .role = sender.String("role"),
            },
            .message = fields.Segments("message"),
            .raw_message = fields.String("raw_message"),
            .raw = *data,
        };
        if (!fields.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_msg", fields.Problem(), *data) };
        }
        if (!sender.Problem().empty()) {
            co_return std::unexpected{ details::invalid_data_error("get_msg", std::format("sender.{}", sender.Problem()), *data) };
        }
        co_return info;
    }

} // namespace drogon_onebot
