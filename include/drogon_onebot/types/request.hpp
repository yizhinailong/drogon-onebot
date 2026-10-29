/**
 * @file request.hpp
 * @brief Declare the request types of the actions that answer friend and group requests.
 */
#pragma once

#include <string>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::SetFriendAddRequest()`.
     */
    struct SetFriendAddRequestParams {
        std::string flag;       ///< `flag` of the friend request event.
        bool approve{ true };   ///< Whether to accept the request.
        std::string remark;     ///< Remark for the new friend when accepting; empty sends none.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

} // namespace drogon_onebot
