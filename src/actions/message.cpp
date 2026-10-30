/**
 * @file message.cpp
 * @brief Implement the message actions of `OneBot`.
 */
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <utility>

#include "details/action_call.hpp"
#include "details/field_reader.hpp"
#include "details/json.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {

    /**
     * @brief Read the `data` of a send action.
     * @param data Object to read.
     * @return The sent message, or the first problem found, e.g. `message_id is missing`
     * or `res_id is not a string`.
     */
    auto to_sent_message(Json::Value const& data) -> std::expected<drogon_onebot::SentMessage, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::SentMessage sent{
            .message_id = fields.RequiredInteger<std::int32_t>("message_id"),
            .res_id = fields.String("res_id"),
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return sent;
    }

    /**
     * @brief Read a message object of the shape that `get_msg` returns.
     * @param data Object to read; also a forwarded message from `get_forward_msg`.
     * @return The message, or the first problem found, e.g. `sender.user_id is missing`.
     */
    auto to_message_info(Json::Value const& data) -> std::expected<drogon_onebot::MessageInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::details::FieldReader sender{ drogon_onebot::details::member(data, "sender") };
        drogon_onebot::MessageInfo info{
            .time = fields.Integer<std::int64_t>("time").value_or(0),
            .message_type = fields.RequiredString("message_type"),
            .message_id = fields.RequiredInteger<std::int32_t>("message_id"),
            .real_id = fields.Integer<std::int32_t>("real_id").value_or(0),
            .group_id = fields.Integer<std::int64_t>("group_id"),
            .sender = drogon_onebot::Sender{
                .user_id = sender.RequiredInteger<std::int64_t>("user_id"),
                .nickname = sender.String("nickname"),
                .card = sender.String("card"),
                .role = sender.String("role"),
            },
            .message = fields.Segments("message"),
            .raw_message = fields.String("raw_message"),
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        if (!sender.Problem().empty()) {
            return std::unexpected{ std::format("sender.{}", sender.Problem()) };
        }
        return info;
    }

    /**
     * @brief Read the `data` of `get_forward_msg` in NapCat's shape: go-cqhttp's `messages`.
     * @param data Object to read.
     * @return The forwarded messages, or the first problem found, e.g. `messages[0].message_type is missing`.
     */
    auto to_forward_message(Json::Value const& data) -> std::expected<drogon_onebot::ForwardMessage, std::string> {
        auto messages{ drogon_onebot::details::read_array<drogon_onebot::MessageInfo>(drogon_onebot::details::member(data, "messages"), "messages", to_message_info) };
        if (!messages) {
            return std::unexpected{ std::move(messages.error()) };
        }
        return drogon_onebot::ForwardMessage{ .messages = std::move(*messages) };
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `send_private_msg`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the sent message's ID, or why the action failed.
     */
    auto OneBot::SendPrivateMsg(
        SendPrivateMsgParams params
    ) const -> drogon::Task<Result<SentMessage>> {
        Json::Value body{ Json::objectValue };
        body["user_id"] = params.user_id;
        body["message"] = details::to_json(params.message);
        return details::call_and_read<SentMessage>(m_transport, "send_private_msg", std::move(body), params.timeout, to_sent_message);
    }

    /**
     * @brief Call `send_group_msg`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the sent message's ID, or why the action failed.
     */
    auto OneBot::SendGroupMsg(
        SendGroupMsgParams params
    ) const -> drogon::Task<Result<SentMessage>> {
        Json::Value body{ Json::objectValue };
        body["group_id"] = params.group_id;
        body["message"] = details::to_json(params.message);
        return details::call_and_read<SentMessage>(m_transport, "send_group_msg", std::move(body), params.timeout, to_sent_message);
    }

    /**
     * @brief Call `send_msg`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the sent message's ID, or why the action failed.
     * @note Without `message_type`, NapCat sends to `group_id` when it is set and to
     * `user_id` otherwise. With `private`, it sends to a non-friend `user_id` as a
     * temporary chat through `group_id`.
     */
    auto OneBot::SendMsg(
        SendMsgParams params
    ) const -> drogon::Task<Result<SentMessage>> {
        Json::Value body{ Json::objectValue };
        if (!params.message_type.empty()) {
            body["message_type"] = params.message_type;
        }
        if (params.user_id) {
            body["user_id"] = *params.user_id;
        }
        if (params.group_id) {
            body["group_id"] = *params.group_id;
        }
        body["message"] = details::to_json(params.message);
        return details::call_and_read<SentMessage>(m_transport, "send_msg", std::move(body), params.timeout, to_sent_message);
    }

    /**
     * @brief Call `delete_msg`.
     * @param params Request fields, read before this returns.
     * @return Task yielding nothing once the message is recalled, or why the action failed.
     * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
     * an older ID fails with `ErrorKind::Action`.
     */
    auto OneBot::DeleteMsg(
        DeleteMsgParams params
    ) const -> drogon::Task<Result<void>> {
        Json::Value body{ Json::objectValue };
        body["message_id"] = params.message_id;
        return details::call_and_discard(m_transport, "delete_msg", std::move(body), params.timeout);
    }

    /**
     * @brief Call `get_msg`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the message, or why the action failed.
     * @note NapCat remembers only the message IDs it has seen since it started (5000 by default);
     * an older ID fails with `ErrorKind::Action`.
     */
    auto OneBot::GetMsg(
        GetMsgParams params
    ) const -> drogon::Task<Result<MessageInfo>> {
        Json::Value body{ Json::objectValue };
        body["message_id"] = params.message_id;
        return details::call_and_read<MessageInfo>(m_transport, "get_msg", std::move(body), params.timeout, to_message_info);
    }

    /**
     * @brief Call `get_forward_msg`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the forwarded messages, or why the action failed.
     * @note NapCat answers with go-cqhttp's `messages`, whole messages shaped like those
     * of `get_msg`, instead of the standard's `node` segments; this reads NapCat's shape.
     * Like `GetMsg()`, it fails with `ErrorKind::Action` for a forward that NapCat has not
     * seen since it started.
     */
    auto OneBot::GetForwardMsg(
        GetForwardMsgParams params
    ) const -> drogon::Task<Result<ForwardMessage>> {
        Json::Value body{ Json::objectValue };
        body["id"] = params.id;
        return details::call_and_read<ForwardMessage>(m_transport, "get_forward_msg", std::move(body), params.timeout, to_forward_message);
    }

} // namespace drogon_onebot
