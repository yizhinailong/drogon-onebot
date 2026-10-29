/**
 * @file system.hpp
 * @brief Declare the request and response types of the actions about the OneBot implementation itself.
 */
#pragma once

#include <optional>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::CanSendImage()`.
     */
    struct CanSendImageParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::CanSendRecord()`.
     */
    struct CanSendRecordParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Answer of `can_send_image` or `can_send_record`.
     */
    struct CanSend {
        bool yes{}; ///< Whether the implementation can send that kind of segment; NapCat always sends true.
    };

    /**
     * @brief Arguments of `OneBot::GetStatus()`.
     */
    struct GetStatusParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Running status of the OneBot implementation, from `get_status`.
     */
    struct Status {
        std::optional<bool> online; ///< Whether the QQ account is online; empty when the implementation cannot tell, which it sends as null.
        bool good{};                ///< Whether every module works as expected and the account is online; NapCat always sends true.
        Json::Value raw;            ///< Whole `data` as received, for the fields that the implementation adds, e.g. NapCat's empty `stat`.
    };

} // namespace drogon_onebot
