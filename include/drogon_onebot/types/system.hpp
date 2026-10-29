/**
 * @file system.hpp
 * @brief Declare the request and response types of the actions about the OneBot implementation itself.
 */
#pragma once

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::CanSendImage()`.
     */
    struct CanSendImageParams {
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Answer of `can_send_image`.
     */
    struct CanSend {
        bool yes{}; ///< Whether the implementation can send that kind of segment; NapCat always sends true.
    };

} // namespace drogon_onebot
