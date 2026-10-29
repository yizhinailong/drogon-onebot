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

    /**
     * @brief Arguments of `OneBot::SetGroupAddRequest()`.
     */
    struct SetGroupAddRequestParams {
        std::string flag;       ///< `flag` of the group request event.
        std::string sub_type;   ///< `add` or `invite`, the `sub_type` of the request event; empty sends none. NapCat ignores it.
        bool approve{ true };   ///< Whether to accept the request.
        std::string reason;     ///< Why the request is refused; empty sends none.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

} // namespace drogon_onebot
