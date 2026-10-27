/**
 * @file event_receiver.cpp
 * @brief Implement the receiver behind the route that the OneBot implementation posts events to.
 */
#include "details/event_receiver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include <drogon/utils/coroutine.h>
#include <trantor/utils/Logger.h>
#include <trantor/utils/Utilities.h>

#include "details/json.hpp"

namespace {

    constexpr std::size_t SHA1_BLOCK_SIZE{ 64 };            ///< Block size of SHA-1 in bytes, for HMAC key padding.
    constexpr unsigned char HMAC_INNER_PAD{ 0x36 };         ///< RFC 2104 `ipad` byte.
    constexpr unsigned char HMAC_OUTER_PAD{ 0x5c };         ///< RFC 2104 `opad` byte.
    constexpr std::string_view SIGNATURE_PREFIX{ "sha1=" }; ///< Prefix of the `X-Signature` value.

    /**
     * @brief XOR every byte of a padded HMAC key with a pad byte.
     * @param block Key padded to the SHA-1 block size.
     * @param pad `HMAC_INNER_PAD` or `HMAC_OUTER_PAD`.
     * @return The XORed block as a byte string.
     */
    auto xor_block(
        std::array<unsigned char, SHA1_BLOCK_SIZE> const& block,
        unsigned char pad
    ) -> std::string {
        std::string result{};
        result.reserve(block.size());
        for (auto const byte : block) {
            result.push_back(static_cast<char>(byte ^ pad));
        }
        return result;
    }

    /**
     * @brief Compute HMAC-SHA1 (RFC 2104), the MAC that OneBot v11 uses for `X-Signature`.
     * @param key Secret key.
     * @param message Signed bytes.
     * @return The 20-byte MAC.
     */
    auto hmac_sha1(std::string_view key, std::string_view message) -> trantor::utils::Hash160 {
        std::array<unsigned char, SHA1_BLOCK_SIZE> block{};
        if (key.size() > block.size()) {
            auto const digest{ trantor::utils::sha1(key.data(), key.size()) };
            std::ranges::copy(digest.bytes, block.begin());
        } else {
            std::ranges::copy(key, block.begin());
        }

        auto inner{ xor_block(block, HMAC_INNER_PAD) };
        inner.append(message);
        auto const inner_digest{ trantor::utils::sha1(inner) };

        auto outer{ xor_block(block, HMAC_OUTER_PAD) };
        outer.append(reinterpret_cast<char const*>(inner_digest.bytes), sizeof(inner_digest.bytes));
        return trantor::utils::sha1(outer);
    }

    /**
     * @brief Encode a SHA-1 sized hash as lowercase hex.
     * @param hash Hash to encode.
     * @return 40 lowercase hex digits.
     */
    auto to_hex(trantor::utils::Hash160 const& hash) -> std::string {
        std::string hex{};
        hex.reserve(2 * sizeof(hash.bytes));
        for (auto const byte : hash.bytes) {
            std::format_to(std::back_inserter(hex), "{:02x}", byte);
        }
        return hex;
    }

    /**
     * @brief Check an `X-Signature` value against the body.
     * @param secret Secret shared with the OneBot implementation.
     * @param body Raw request body.
     * @param signature Header value, `sha1=` followed by the hex MAC of `body`.
     * @return True if the MAC matches; hex digits compare case-insensitively and in constant time.
     */
    auto signature_matches(
        std::string_view secret,
        std::string_view body,
        std::string_view signature
    ) -> bool {
        if (!signature.starts_with(SIGNATURE_PREFIX)) {
            return false;
        }
        signature.remove_prefix(SIGNATURE_PREFIX.size());
        auto const expected{ to_hex(hmac_sha1(secret, body)) };
        if (signature.size() != expected.size()) {
            return false;
        }
        unsigned char difference{};
        for (std::size_t i{}; i < expected.size(); ++i) {
            auto const digit{ std::tolower(static_cast<unsigned char>(signature[i])) };
            difference |= static_cast<unsigned char>(digit ^ expected[i]);
        }
        return difference == 0;
    }

    /**
     * @brief Build a plain-text response that explains a rejected event.
     * @param code HTTP status.
     * @param body Explanation.
     * @return Response with a `text/plain` body.
     */
    auto text_response(drogon::HttpStatusCode code, std::string body) -> drogon::HttpResponsePtr {
        auto response{ drogon::HttpResponse::newHttpResponse(code, drogon::CT_TEXT_PLAIN) };
        response->setBody(std::move(body));
        return response;
    }

    /**
     * @brief Read typed members of one JSON object, keeping the first problem found.
     *
     * A member that is missing or null reads as absent; a member of the wrong type is a problem.
     */
    class FieldReader {
    private:
        Json::Value const& m_object; ///< Object to read; outlives the reader.
        std::string m_problem;       ///< First problem found; empty while every read succeeded.

    public:
        /**
         * @brief Read members of `object`.
         * @param object Object to read; a non-object reads as an object without members.
         */
        explicit FieldReader(Json::Value const& object)
            : m_object{ object } {}

        /**
         * @brief Read an optional integer member.
         * @tparam T Integer type to return.
         * @param key Member name.
         * @return The integer, or empty when the member is absent or invalid.
         */
        template <std::integral T>
        [[nodiscard]] auto Integer(char const* key) -> std::optional<T> {
            auto const& value{ drogon_onebot::details::member(m_object, key) };
            if (value.isNull()) {
                return std::nullopt;
            }
            auto const number{ drogon_onebot::details::read_integer<T>(value) };
            if (!number) {
                fail(std::format("{} is not an integer in range", key));
            }
            return number;
        }

        /**
         * @brief Read a required integer member.
         * @tparam T Integer type to return.
         * @param key Member name.
         * @return The integer, or 0 after recording a problem.
         */
        template <std::integral T>
        [[nodiscard]] auto RequiredInteger(char const* key) -> T {
            auto const number{ Integer<T>(key) };
            if (!number) {
                fail(std::format("{} is missing", key));
            }
            return number.value_or(T{});
        }

        /**
         * @brief Read an optional string member.
         * @param key Member name.
         * @return The string, or empty when the member is absent or invalid.
         */
        [[nodiscard]] auto String(char const* key) -> std::string {
            auto const& value{ drogon_onebot::details::member(m_object, key) };
            if (value.isNull()) {
                return {};
            }
            if (!value.isString()) {
                fail(std::format("{} is not a string", key));
                return {};
            }
            return value.asString();
        }

        /**
         * @brief Read a required string member.
         * @param key Member name.
         * @return The string, or empty after recording a problem.
         */
        [[nodiscard]] auto RequiredString(char const* key) -> std::string {
            if (drogon_onebot::details::member(m_object, key).isNull()) {
                fail(std::format("{} is missing", key));
                return {};
            }
            return String(key);
        }

        /**
         * @brief Read a required message member in array format.
         * @param key Member name.
         * @return The segments, or none after recording a problem.
         */
        [[nodiscard]] auto Segments(char const* key) -> drogon_onebot::Message {
            auto message{ drogon_onebot::details::parse_message(drogon_onebot::details::member(m_object, key)) };
            if (!message) {
                fail(std::format("{} is not an array of message segments (set messagePostFormat to array in NapCat)", key));
                return {};
            }
            return std::move(*message);
        }

        /**
         * @brief Return the first problem found.
         * @return Description of the problem; empty while every read succeeded.
         */
        [[nodiscard]] auto Problem() const noexcept -> std::string const& {
            return m_problem;
        }

    private:
        /**
         * @brief Record a problem unless an earlier one is already recorded.
         * @param problem Description of the problem.
         */
        auto fail(std::string problem) -> void {
            if (m_problem.empty()) {
                m_problem = std::move(problem);
            }
        }
    };

    /**
     * @brief Return a parsed event, or the first problem its reader found.
     * @tparam T Event type.
     * @param fields Reader that filled `event`.
     * @param kind Event kind for the problem description, e.g. `notice`.
     * @param event Parsed event.
     * @return `event`, or a description of the problem.
     */
    template <typename T>
    auto finish(
        FieldReader const& fields,
        std::string_view kind,
        T event
    ) -> std::expected<T, std::string> {
        if (!fields.Problem().empty()) {
            return std::unexpected{ std::format("{} event: {}", kind, fields.Problem()) };
        }
        return event;
    }

    /**
     * @brief Parse an event whose `post_type` is `message`.
     * @param root Event object.
     * @return The event, or why it is not a valid message event.
     */
    auto parse_message_event(
        Json::Value const& root
    ) -> std::expected<drogon_onebot::MessageEvent, std::string> {
        FieldReader fields{ root };
        FieldReader sender{ drogon_onebot::details::member(root, "sender") };
        drogon_onebot::MessageEvent event{
            .time = fields.Integer<std::int64_t>("time").value_or(0),
            .self_id = fields.Integer<std::int64_t>("self_id").value_or(0),
            .message_type = fields.RequiredString("message_type"),
            .sub_type = fields.String("sub_type"),
            .message_id = fields.Integer<std::int32_t>("message_id").value_or(0),
            .user_id = fields.RequiredInteger<std::int64_t>("user_id"),
            .group_id = fields.Integer<std::int64_t>("group_id"),
            .message = fields.Segments("message"),
            .raw_message = fields.String("raw_message"),
            .font = fields.Integer<std::int32_t>("font").value_or(0),
            .sender = drogon_onebot::Sender{
                .user_id = sender.Integer<std::int64_t>("user_id").value_or(0),
                .nickname = sender.String("nickname"),
                .card = sender.String("card"),
                .role = sender.String("role"),
            },
            .raw = root,
        };
        if (!sender.Problem().empty()) {
            return std::unexpected{ std::format("message event: sender.{}", sender.Problem()) };
        }
        if (fields.Problem().empty() && event.message_type == "group" && !event.group_id) {
            return std::unexpected{ std::string{ "message event: group_id is missing" } };
        }
        return finish(fields, "message", std::move(event));
    }

    /**
     * @brief Parse an event whose `post_type` is `notice`.
     * @param root Event object.
     * @return The event, or why it is not a valid notice event.
     */
    auto parse_notice_event(
        Json::Value const& root
    ) -> std::expected<drogon_onebot::NoticeEvent, std::string> {
        FieldReader fields{ root };
        drogon_onebot::NoticeEvent event{
            .time = fields.Integer<std::int64_t>("time").value_or(0),
            .self_id = fields.Integer<std::int64_t>("self_id").value_or(0),
            .notice_type = fields.RequiredString("notice_type"),
            .sub_type = fields.String("sub_type"),
            .raw = root,
        };
        return finish(fields, "notice", std::move(event));
    }

    /**
     * @brief Parse an event whose `post_type` is `request`.
     * @param root Event object.
     * @return The event, or why it is not a valid request event.
     */
    auto parse_request_event(
        Json::Value const& root
    ) -> std::expected<drogon_onebot::RequestEvent, std::string> {
        FieldReader fields{ root };
        drogon_onebot::RequestEvent event{
            .time = fields.Integer<std::int64_t>("time").value_or(0),
            .self_id = fields.Integer<std::int64_t>("self_id").value_or(0),
            .request_type = fields.RequiredString("request_type"),
            .sub_type = fields.String("sub_type"),
            .raw = root,
        };
        return finish(fields, "request", std::move(event));
    }

    /**
     * @brief Parse an event whose `post_type` is `meta_event`.
     * @param root Event object.
     * @return The event, or why it is not a valid meta event.
     */
    auto parse_meta_event(
        Json::Value const& root
    ) -> std::expected<drogon_onebot::MetaEvent, std::string> {
        FieldReader fields{ root };
        drogon_onebot::MetaEvent event{
            .time = fields.Integer<std::int64_t>("time").value_or(0),
            .self_id = fields.Integer<std::int64_t>("self_id").value_or(0),
            .meta_event_type = fields.RequiredString("meta_event_type"),
            .sub_type = fields.String("sub_type"),
            .raw = root,
        };
        return finish(fields, "meta", std::move(event));
    }

    /**
     * @brief Parsed event; `std::monostate` for a `post_type` without handlers.
     */
    using Event = std::variant<
        std::monostate,
        drogon_onebot::MessageEvent,
        drogon_onebot::NoticeEvent,
        drogon_onebot::RequestEvent,
        drogon_onebot::MetaEvent>;

    /**
     * @brief Parse a posted event by its `post_type`.
     * @param root Parsed request body.
     * @return The event (`std::monostate` for an unknown `post_type`), or why the body is not an event.
     */
    auto parse_event(Json::Value const& root) -> std::expected<Event, std::string> {
        if (!root.isObject()) {
            return std::unexpected{ std::string{ "event is not a JSON object" } };
        }
        FieldReader fields{ root };
        auto const post_type{ fields.RequiredString("post_type") };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        if (post_type == "message") {
            return parse_message_event(root);
        }
        if (post_type == "notice") {
            return parse_notice_event(root);
        }
        if (post_type == "request") {
            return parse_request_event(root);
        }
        if (post_type == "meta_event") {
            return parse_meta_event(root);
        }
        LOG_DEBUG << "drogon_onebot::OneBotClient: dropped an event with post_type " << post_type;
        return Event{};
    }

    /**
     * @brief Start every handler for one event, each in its own task.
     * @tparam T Event type the handlers take.
     * @param handlers Handlers to start; each task runs its own copy.
     * @param event Event to pass; each task gets its own copy.
     * @param kind Event kind for log messages, e.g. `message`.
     */
    template <typename T>
    auto dispatch(
        std::vector<std::function<drogon::Task<>(T)>> const& handlers,
        T const& event,
        std::string_view kind
    ) -> void {
        for (auto const& handler : handlers) {
            // async_run moves this lambda into its coroutine frame, so the copies of the
            // handler (with its captures) and the event live until the task completes.
            drogon::async_run([handler, event, kind]() -> drogon::Task<> {
                try {
                    co_await handler(event);
                } catch (std::exception const& e) {
                    LOG_ERROR << "drogon_onebot::OneBotClient: " << kind << " handler threw: " << e.what();
                } catch (...) {
                    LOG_ERROR << "drogon_onebot::OneBotClient: " << kind << " handler threw a non-standard exception";
                }
            });
        }
    }

} // namespace

namespace drogon_onebot::details {

    /**
     * @brief Create a receiver without handlers.
     * @param secret Secret that signs events; empty accepts unsigned events.
     */
    EventReceiver::EventReceiver(std::string secret)
        : m_secret{ std::move(secret) } {}

    /**
     * @brief Add a handler for message events.
     * @param handler Coroutine to run for each message event.
     */
    auto EventReceiver::AddMessageHandler(MessageHandler handler) -> void {
        m_message_handlers.push_back(std::move(handler));
    }

    /**
     * @brief Add a handler for notice events.
     * @param handler Coroutine to run for each notice event.
     */
    auto EventReceiver::AddNoticeHandler(NoticeHandler handler) -> void {
        m_notice_handlers.push_back(std::move(handler));
    }

    /**
     * @brief Add a handler for request events.
     * @param handler Coroutine to run for each request event.
     */
    auto EventReceiver::AddRequestHandler(RequestHandler handler) -> void {
        m_request_handlers.push_back(std::move(handler));
    }

    /**
     * @brief Add a handler for meta events.
     * @param handler Coroutine to run for each meta event.
     */
    auto EventReceiver::AddMetaEventHandler(MetaEventHandler handler) -> void {
        m_meta_event_handlers.push_back(std::move(handler));
    }

    /**
     * @brief Answer one posted event, then start its handlers.
     * @param req Request from the OneBot implementation.
     * @param callback Drogon's response callback, called exactly once before any handler runs.
     * @note Event types other than the four above, such as NapCat's `message_sent`,
     * are answered with 204 and dropped.
     */
    auto EventReceiver::Receive(
        drogon::HttpRequestPtr const& req,
        std::function<void(drogon::HttpResponsePtr const&)>&& callback
    ) const -> void {
        if (!m_secret.empty()) {
            auto const& signature{ req->getHeader("x-signature") };
            if (signature.empty()) {
                LOG_WARN << "drogon_onebot::OneBotClient: rejected an event without X-Signature";
                callback(text_response(drogon::k401Unauthorized, "missing X-Signature"));
                return;
            }
            if (!signature_matches(m_secret, req->body(), signature)) {
                LOG_WARN << "drogon_onebot::OneBotClient: rejected an event whose X-Signature does not match";
                callback(text_response(drogon::k403Forbidden, "X-Signature does not match the body"));
                return;
            }
        }

        auto const& json{ req->getJsonObject() };
        if (json == nullptr) {
            LOG_WARN << "drogon_onebot::OneBotClient: rejected an event body that is not JSON: " << req->getJsonError();
            callback(text_response(drogon::k400BadRequest, std::format("event body is not JSON: {}", req->getJsonError())));
            return;
        }
        auto const event{ parse_event(*json) };
        if (!event) {
            LOG_WARN << "drogon_onebot::OneBotClient: rejected an event: " << event.error();
            callback(text_response(drogon::k400BadRequest, event.error()));
            return;
        }

        // Answer first: NapCat keeps its request open until this response arrives, and a
        // handler may await for seconds, e.g. on an LLM.
        callback(drogon::HttpResponse::newHttpResponse(drogon::k204NoContent, drogon::CT_NONE));

        if (auto const* message{ std::get_if<MessageEvent>(&*event) }) {
            dispatch(m_message_handlers, *message, "message");
        } else if (auto const* notice{ std::get_if<NoticeEvent>(&*event) }) {
            dispatch(m_notice_handlers, *notice, "notice");
        } else if (auto const* request{ std::get_if<RequestEvent>(&*event) }) {
            dispatch(m_request_handlers, *request, "request");
        } else if (auto const* meta{ std::get_if<MetaEvent>(&*event) }) {
            dispatch(m_meta_event_handlers, *meta, "meta");
        }
    }

} // namespace drogon_onebot::details
