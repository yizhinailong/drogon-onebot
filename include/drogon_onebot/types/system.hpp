/**
 * @file system.hpp
 * @brief Declare the request and response types of the actions about the OneBot implementation itself.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

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

    /**
     * @brief Arguments of `OneBot::GetVersionInfo()`.
     */
    struct GetVersionInfoParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Version of the OneBot implementation, from `get_version_info`.
     */
    struct VersionInfo {
        std::string app_name;         ///< Name of the implementation; NapCat sends `NapCat.Onebot`.
        std::string app_version;      ///< Version of the implementation, e.g. `1.2.3`.
        std::string protocol_version; ///< OneBot version that it implements; NapCat sends `v11`.
        Json::Value raw;              ///< Whole `data` as received, for the fields that the implementation adds.
    };

    /**
     * @brief Arguments of `OneBot::SetRestart()`.
     */
    struct SetRestartParams {
        std::int32_t delay{};   ///< Milliseconds to wait before restarting, 0 by default as in the standard; NapCat ignores it.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::CleanCache()`.
     */
    struct CleanCacheParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

} // namespace drogon_onebot
