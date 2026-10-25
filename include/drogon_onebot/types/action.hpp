/**
 * @file action.hpp
 * @brief Declare the request type of a raw OneBot action call.
 */
#pragma once

#include <string>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::CallAction()`.
     *
     * Reaches actions that have no typed method, including implementation extensions.
     */
    struct CallActionParams {
        std::string action;                      ///< Action name, e.g. `get_status`; sent as `POST <base_url>/<action>`.
        Json::Value params{ Json::objectValue }; ///< Action parameters, sent as the JSON request body.
        double timeout{ 30.0 };                  ///< Seconds to wait for the response; 0 waits forever.
    };

} // namespace drogon_onebot
