/**
 * @file transport.cpp
 * @brief Implement the HTTP transport that the copies of one `OneBot` share.
 */
#include "details/transport.hpp"

#include <format>
#include <string_view>
#include <tuple>
#include <utility>

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

namespace {

    /**
     * @brief Split a base URL into the host part for HttpClient and the path prefix.
     * @param base_url Base URL such as `http://127.0.0.1:3000/onebot`; a trailing `/` is ignored.
     * @return Host and path prefix, e.g. `http://127.0.0.1:3000` and `/onebot`; the prefix is empty without a path.
     */
    auto split_base_url(std::string_view base_url) -> std::pair<std::string, std::string> {
        if (base_url.ends_with('/')) {
            base_url.remove_suffix(1);
        }
        auto const scheme_end{ base_url.find("://") };
        auto const path_begin{ base_url.find('/', scheme_end == std::string_view::npos ? 0 : scheme_end + 3) };
        if (path_begin == std::string_view::npos) {
            return { std::string{ base_url }, std::string{} };
        }
        return {
            std::string{ base_url.substr(0, path_begin) },
            std::string{ base_url.substr(path_begin) }
        };
    }

    /**
     * @brief Read the error text of a OneBot response envelope.
     * @param root Parsed response body.
     * @return The `message` field, else the `wording` field, else an empty string.
     */
    auto envelope_message(Json::Value const& root) -> std::string {
        if (root["message"].isString() && !root["message"].asString().empty()) {
            return root["message"].asString();
        }
        if (root["wording"].isString()) {
            return root["wording"].asString();
        }
        return {};
    }

    /**
     * @brief Describe a non-2xx response.
     * @param response Response from the OneBot implementation.
     * @return The envelope's error text when the body has one, otherwise the raw body.
     */
    auto status_error_message(drogon::HttpResponse const& response) -> std::string {
        auto const& json{ response.getJsonObject() };
        if (json != nullptr && json->isObject()) {
            if (auto message{ envelope_message(*json) }; !message.empty()) {
                return message;
            }
        }
        return std::string{ response.body() };
    }

    /**
     * @brief Pick the event loop for a new HTTP client.
     * @return The calling thread's loop, or the app's main loop when the thread runs none.
     */
    auto current_loop() -> trantor::EventLoop* {
        auto* const loop{ trantor::EventLoop::getEventLoopOfCurrentThread() };
        return loop != nullptr ? loop : drogon::app().getLoop();
    }

} // namespace

namespace drogon_onebot::details {

    namespace {

        /**
         * @brief Take `data` out of a OneBot response envelope.
         * @param action Action name, for error messages.
         * @param root Parsed body of a 2xx response.
         * @return `data` when `status` is `ok` or `async`; an `Action` error when it is
         * `failed`; a `ResponseValidation` error when the body is not an envelope.
         */
        auto unwrap_envelope(std::string const& action, Json::Value const& root) -> Result<Json::Value> {
            if (!root.isObject() || !root["status"].isString()) {
                return std::unexpected{ Error{
                    .kind = ErrorKind::ResponseValidation,
                    .message = std::format("{}: response has no status string", action),
                    .body = root.toStyledString(),
                } };
            }
            auto const status{ root["status"].asString() };
            if (status == "ok" || status == "async") {
                return root["data"];
            }
            if (status == "failed") {
                auto const retcode{ root["retcode"].isInt() ? root["retcode"].asInt() : 0 };
                return std::unexpected{ Error{
                    .kind = ErrorKind::Action,
                    .message = std::format("{}: retcode {}: {}", action, retcode, envelope_message(root)),
                    .retcode = retcode,
                    .body = root.toStyledString(),
                } };
            }
            return std::unexpected{ Error{
                .kind = ErrorKind::ResponseValidation,
                .message = std::format("{}: unknown status {}", action, status),
                .body = root.toStyledString(),
            } };
        }

    } // namespace

    /**
     * @brief Store the endpoint settings and split `config.base_url`.
     * @param config Endpoint settings; `access_token` may be empty for endpoints without auth.
     */
    Transport::Transport(OneBotConfig config)
        : m_config{ std::move(config) } {
        std::tie(m_host, m_path_prefix) = split_base_url(m_config.base_url);
    }

    /**
     * @brief Return the endpoint settings.
     * @return Settings passed to the constructor.
     */
    auto Transport::Config() const noexcept -> OneBotConfig const& {
        return m_config;
    }

    /**
     * @brief POST an action's parameters and await the `data` of its response envelope.
     * @param action Action name, e.g. `send_private_msg`; the request goes to `<base_url>/<action>`.
     * @param params Action parameters, sent as the JSON request body.
     * @param timeout Seconds to wait for the response; 0 waits forever.
     * @return Task yielding the envelope's `data` when `status` is `ok` or `async`, or why the action failed.
     * @note The caller keeps this object alive until the task completes.
     */
    auto Transport::Call(
        std::string action,
        Json::Value params,
        double timeout
    ) const -> drogon::Task<Result<Json::Value>> {
        auto request{ drogon::HttpRequest::newHttpJsonRequest(params) };
        request->setMethod(drogon::Post);
        request->setPath(std::format("{}/{}", m_path_prefix, action));
        if (!m_config.access_token.empty()) {
            request->addHeader("Authorization", "Bearer " + m_config.access_token);
        }

        // Keep the client in the coroutine frame until the response arrives.
        auto const client{ drogon::HttpClient::newHttpClient(m_host, current_loop()) };
        drogon::HttpResponsePtr response{};
        try {
            response = co_await client->sendRequestCoro(request, timeout);
        } catch (drogon::HttpException const& e) {
            co_return std::unexpected{ Error{
                .kind = e.code() == drogon::ReqResult::Timeout ? ErrorKind::Timeout : ErrorKind::Connection,
                .message = std::format("POST {}/{}: {}", m_config.base_url, action, e.what()),
            } };
        }

        auto const status{ static_cast<int>(response->getStatusCode()) };
        if (status < 200 || status >= 300) {
            co_return std::unexpected{ Error{
                .kind = ErrorKind::Status,
                .message = std::format("{}: HTTP {}: {}", action, status, status_error_message(*response)),
                .status_code = status,
                .body = std::string{ response->body() },
            } };
        }

        auto const& json{ response->getJsonObject() };
        if (json == nullptr) {
            co_return std::unexpected{ Error{
                .kind = ErrorKind::ResponseValidation,
                .message = std::format("{}: response body is not JSON: {}", action, response->getJsonError()),
                .body = std::string{ response->body() },
            } };
        }
        co_return unwrap_envelope(action, *json);
    }

} // namespace drogon_onebot::details
