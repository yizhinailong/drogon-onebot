/**
 * @file message.hpp
 * @brief Declare the message segment type and the request and response types of the message actions.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief One segment of a message in OneBot v11 array format.
     *
     * Serialized as `{ "type": <type>, "data": <data> }`.
     */
    struct MessageSegment {
        std::string type;                      ///< Segment type, e.g. `text`, `face`, `image`, or `at`.
        Json::Value data{ Json::objectValue }; ///< Segment fields, e.g. `{ "text": "Hello!" }` for `text`.
    };

    /**
     * @brief Message as an ordered list of segments.
     */
    using Message = std::vector<MessageSegment>;

    /**
     * @brief Arguments of `OneBot::SendPrivateMsg()`.
     */
    struct SendPrivateMsgParams {
        std::int64_t user_id{}; ///< QQ number of the recipient.
        Message message;        ///< Message to send.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::SendGroupMsg()`.
     */
    struct SendGroupMsgParams {
        std::int64_t group_id{}; ///< Group number to send to.
        Message message;         ///< Message to send.
        double timeout{ 30.0 };  ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Message the OneBot implementation sent, from `send_private_msg` or `send_group_msg`.
     */
    struct SentMessage {
        std::int32_t message_id{}; ///< ID of the sent message, e.g. for `delete_msg`.
    };

} // namespace drogon_onebot
