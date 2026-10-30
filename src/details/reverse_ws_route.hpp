/**
 * @file reverse_ws_route.hpp
 * @brief Declare the drogon middleware and controller behind the reverse WebSocket route.
 */
#pragma once

#include <memory>
#include <string>

#include <drogon/HttpMiddleware.h>
#include <drogon/WebSocketController.h>

#include "details/reverse_ws.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Check the handshake of a reverse WebSocket connection before drogon upgrades it.
     *
     * drogon answers `101 Switching Protocols` before the controller sees the connection
     * (`HttpServer.cc:761-770`), so the checks run in this middleware on the route. It answers
     * 400 when `X-Client-Role` is set but not `Universal`, the only role that NapCat sends. With
     * an access token, it answers 401 when `Authorization` is missing and 403 when it is not
     * `Bearer <access_token>`, as the HTTP API of the standard does.
     */
    class ReverseWsHandshake final : public drogon::HttpMiddleware<ReverseWsHandshake, false> {
    private:
        std::string m_access_token; ///< Token that connections must present; empty accepts any.

    public:
        /**
         * @brief Create the middleware.
         * @param access_token Token that connections must present; empty accepts any.
         */
        explicit ReverseWsHandshake(std::string access_token);

        /**
         * @brief Reject a handshake that fails the checks, or pass it on to the controller.
         * @param req Upgrade request.
         * @param next Continues to the controller, which upgrades the connection.
         * @param callback Sends the rejection.
         */
        auto invoke(
            drogon::HttpRequestPtr const& req,
            drogon::MiddlewareNextCallback&& next,
            drogon::MiddlewareCallback&& callback
        ) -> void override;
    };

    /**
     * @brief Hand the connections of the reverse WebSocket route and their messages to a `ReverseWebSocket`.
     *
     * drogon looks controllers up by class name (`HttpControllersRouter.cc:275-287`), so the
     * plugin registers this instance with `registerController()` and then adds the route from
     * its config with `registerWebSocketController()`.
     */
    class ReverseWsController final : public drogon::WebSocketController<ReverseWsController, false> {
    private:
        std::shared_ptr<ReverseWebSocket> m_socket; ///< Endpoint that keeps the connection.

    public:
        /**
         * @brief Register no route, because `ws_path` comes from the plugin config.
         * @note `registerController()` calls this.
         */
        static auto initPathRouting() -> void;

        /**
         * @brief Create the controller.
         * @param socket Endpoint that keeps the connection.
         */
        explicit ReverseWsController(std::shared_ptr<ReverseWebSocket> socket);

        /**
         * @brief Pass a text or binary message to the endpoint.
         * @param connection Connection that received it.
         * @param message Message payload.
         * @param type Message type; drogon also reports pings, pongs, and close frames here.
         */
        auto handleNewMessage(
            drogon::WebSocketConnectionPtr const& connection,
            std::string&& message,
            drogon::WebSocketMessageType const& type
        ) -> void override;

        /**
         * @brief Make an upgraded connection the one that calls go out on.
         * @param req Upgrade request, which passed `ReverseWsHandshake`.
         * @param connection New connection.
         */
        auto handleNewConnection(
            drogon::HttpRequestPtr const& req,
            drogon::WebSocketConnectionPtr const& connection
        ) -> void override;

        /**
         * @brief Tell the endpoint that a connection closed.
         * @param connection Closed connection.
         */
        auto handleConnectionClosed(drogon::WebSocketConnectionPtr const& connection) -> void override;
    };

} // namespace drogon_onebot::details
