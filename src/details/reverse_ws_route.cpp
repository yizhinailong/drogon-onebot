/**
 * @file reverse_ws_route.cpp
 * @brief Implement the drogon middleware and controller behind the reverse WebSocket route.
 */
#include "details/reverse_ws_route.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

#include <drogon/HttpResponse.h>
#include <trantor/utils/Logger.h>

namespace {

    constexpr std::string_view BEARER_PREFIX{ "Bearer " };    ///< Scheme prefix of the `Authorization` value.
    constexpr std::string_view UNIVERSAL_ROLE{ "Universal" }; ///< The only `X-Client-Role` accepted.

    /**
     * @brief Compare two strings without stopping at the first difference.
     * @param lhs First string.
     * @param rhs Second string.
     * @return True if both are equal; only the length comparison is not constant-time.
     */
    auto equal_in_constant_time(std::string_view lhs, std::string_view rhs) -> bool {
        if (lhs.size() != rhs.size()) {
            return false;
        }
        unsigned char difference{};
        for (std::size_t i{}; i < lhs.size(); ++i) {
            difference |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);
        }
        return difference == 0;
    }

    /**
     * @brief Build a plain-text response that explains a rejected handshake.
     * @param code HTTP status.
     * @param body Explanation.
     * @return Response with a `text/plain` body.
     */
    auto text_response(drogon::HttpStatusCode code, std::string body) -> drogon::HttpResponsePtr {
        auto response{ drogon::HttpResponse::newHttpResponse(code, drogon::CT_TEXT_PLAIN) };
        response->setBody(std::move(body));
        return response;
    }

} // namespace

namespace drogon_onebot::details {

    /**
     * @brief Create the middleware.
     * @param access_token Token that connections must present; empty accepts any.
     */
    ReverseWsHandshake::ReverseWsHandshake(std::string access_token)
        : m_access_token{ std::move(access_token) } {}

    /**
     * @brief Reject a handshake that fails the checks, or pass it on to the controller.
     * @param req Upgrade request.
     * @param next Continues to the controller, which upgrades the connection.
     * @param callback Sends the rejection.
     */
    auto ReverseWsHandshake::invoke(
        drogon::HttpRequestPtr const& req,
        drogon::MiddlewareNextCallback&& next,
        drogon::MiddlewareCallback&& callback
    ) -> void {
        auto const& role{ req->getHeader("x-client-role") };
        if (!role.empty() && role != UNIVERSAL_ROLE) {
            LOG_WARN << "drogon_onebot::OneBotClient: rejected a reverse WebSocket connection with X-Client-Role " << role;
            callback(text_response(drogon::k400BadRequest, "X-Client-Role must be Universal"));
            return;
        }
        if (!m_access_token.empty()) {
            std::string_view const authorization{ req->getHeader("authorization") };
            if (authorization.empty()) {
                LOG_WARN << "drogon_onebot::OneBotClient: rejected a reverse WebSocket connection without Authorization";
                callback(text_response(drogon::k401Unauthorized, "missing Authorization"));
                return;
            }
            if (!authorization.starts_with(BEARER_PREFIX) || !equal_in_constant_time(authorization.substr(BEARER_PREFIX.size()), m_access_token)) {
                LOG_WARN << "drogon_onebot::OneBotClient: rejected a reverse WebSocket connection whose access token does not match";
                callback(text_response(drogon::k403Forbidden, "Authorization does not carry the access token"));
                return;
            }
        }
        next(std::move(callback));
    }

    /**
     * @brief Register no route, because `ws_path` comes from the plugin config.
     * @note `registerController()` calls this.
     */
    auto ReverseWsController::initPathRouting() -> void {}

    /**
     * @brief Create the controller.
     * @param socket Endpoint that keeps the connection.
     */
    ReverseWsController::ReverseWsController(std::shared_ptr<ReverseWebSocket> socket)
        : m_socket{ std::move(socket) } {}

    /**
     * @brief Pass a text or binary message to the endpoint.
     * @param connection Connection that received it.
     * @param message Message payload.
     * @param type Message type; drogon also reports pings, pongs, and close frames here.
     */
    auto ReverseWsController::handleNewMessage(
        drogon::WebSocketConnectionPtr const& connection,
        std::string&& message,
        drogon::WebSocketMessageType const& type
    ) -> void {
        if (type == drogon::WebSocketMessageType::Text || type == drogon::WebSocketMessageType::Binary) {
            m_socket->Receive(message);
        }
    }

    /**
     * @brief Make an upgraded connection the one that calls go out on.
     * @param req Upgrade request, which passed `ReverseWsHandshake`.
     * @param connection New connection.
     */
    auto ReverseWsController::handleNewConnection(
        drogon::HttpRequestPtr const& req,
        drogon::WebSocketConnectionPtr const& connection
    ) -> void {
        m_socket->Open(connection, req->getHeader("x-self-id"));
    }

    /**
     * @brief Tell the endpoint that a connection closed.
     * @param connection Closed connection.
     */
    auto ReverseWsController::handleConnectionClosed(drogon::WebSocketConnectionPtr const& connection) -> void {
        m_socket->Close(connection);
    }

} // namespace drogon_onebot::details
