/**
 * @file support.hpp
 * @brief Share the helpers of the test programs: environment variables, the app thread, and event inboxes.
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <json/json.h>

namespace test_support {

    constexpr auto LISTEN_DELAY{ std::chrono::milliseconds{ 200 } };  ///< Wait for the listeners after the beginning advices.
    constexpr auto EVENT_WAIT{ std::chrono::milliseconds{ 2000 } };   ///< Longest wait for a handler to receive an event.
    constexpr auto NO_EVENT_WAIT{ std::chrono::milliseconds{ 300 } }; ///< Wait after which a handler that has not run counts as not run.
    constexpr char const* SERVER_URL{ "http://127.0.0.1:18080" };     ///< Listener in config.json, which its `base_url` points at.
    constexpr char const* EVENT_PATH{ "/onebot/event" };              ///< `event_path` in config.json.
    constexpr char const* WS_PATH{ "/onebot/ws" };                    ///< `ws_path` in config.json.
    constexpr char const* SECRET_ENV{ "DROGON_ONEBOT_TEST_SECRET" };  ///< `secret_env` in config.json.
    constexpr char const* SECRET{ "drogon-onebot-test-secret" };      ///< Value that the tests give `SECRET_ENV`.

    /**
     * @brief Set an environment variable of this process.
     * @param name Variable name.
     * @param value New value.
     */
    inline auto set_env(char const* name, char const* value) -> void {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    /**
     * @brief Remove an environment variable of this process.
     * @param name Variable name.
     */
    inline auto unset_env(char const* name) -> void {
#ifdef _WIN32
        _putenv_s(name, "");
#else
        unsetenv(name);
#endif
    }

    /**
     * @brief Parse config.json from the working directory, which CTest and `just run` set to the repo root.
     * @return The whole configuration.
     * @throws Json::RuntimeError If the file is missing or not valid JSON.
     */
    inline auto read_config() -> Json::Value {
        std::ifstream file{ "config.json" };
        Json::Value root{};
        file >> root;
        return root;
    }

    /**
     * @brief Run the app on a background thread, run every `DROGON_TEST`, then stop the app.
     *
     * Follows drogon's own app-backed tests (`lib/tests/main_CookieSameSite.cc`). Beginning
     * advices run before `startListening()`, and each IO loop starts listening
     * asynchronously, so the tests start `LISTEN_DELAY` after the advices. The test thread
     * runs no event loop, so tests may block on `drogon::sync_wait()`.
     * @param argc Argument count, forwarded to `drogon::test::run()`.
     * @param argv Arguments, forwarded to `drogon::test::run()`: `-s` prints passing checks,
     * `-r <name>` runs one test, and `-l` lists them.
     * @return 0 if every check passed, 1 otherwise.
     */
    inline auto run_tests_with_app(int argc, char** argv) -> int {
        std::promise<void> started{};
        drogon::app().registerBeginningAdvice([&started] { started.set_value(); });
        std::thread server{ [] { drogon::app().run(); } };
        started.get_future().wait();
        std::this_thread::sleep_for(LISTEN_DELAY);

        int const status{ drogon::test::run(argc, argv) };
        drogon::app().getLoop()->queueInLoop([] { drogon::app().quit(); });
        server.join();
        return status;
    }

    /**
     * @brief POST a raw event body to the plugin's event route and wait for the answer.
     *
     * Blocks on `HttpClient::sendRequest`, which is safe on the test thread because the
     * client runs on the app's main loop.
     * @param body Raw request body, signed by the caller as is.
     * @param signature `X-Signature` value; empty sends no header.
     * @return The route's response.
     * @throws std::runtime_error If no response arrives.
     */
    inline auto post_event(
        std::string const& body,
        std::string const& signature = {}
    ) -> drogon::HttpResponsePtr {
        auto request{ drogon::HttpRequest::newHttpRequest() };
        request->setMethod(drogon::Post);
        request->setPath(EVENT_PATH);
        request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        request->setBody(body);
        if (!signature.empty()) {
            request->addHeader("X-Signature", signature);
        }
        auto const client{ drogon::HttpClient::newHttpClient(SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ std::format("POST {}: {}", EVENT_PATH, drogon::to_string_view(result)) };
        }
        return response;
    }

    /**
     * @brief Serialize an event the way NapCat does, without whitespace.
     * @param event Event object.
     * @return Compact JSON text.
     */
    inline auto to_body(Json::Value const& event) -> std::string {
        Json::StreamWriterBuilder builder{};
        builder["indentation"] = "";
        return Json::writeString(builder, event);
    }

    /**
     * @brief Queue that event handlers fill on IO threads and tests drain on the test thread.
     * @tparam T Item type.
     */
    template <typename T>
    class Inbox {
    private:
        std::mutex m_mutex;              ///< Guards `m_items`.
        std::condition_variable m_ready; ///< Signaled after each `Push()`.
        std::deque<T> m_items;           ///< Items not yet taken, oldest first.

    public:
        /**
         * @brief Append an item and wake a waiting `Pop()`.
         * @param item Item to append.
         */
        auto Push(T item) -> void {
            {
                std::lock_guard lock{ m_mutex };
                m_items.push_back(std::move(item));
            }
            m_ready.notify_all();
        }

        /**
         * @brief Take the oldest item, waiting for one if the queue is empty.
         * @param timeout Longest wait.
         * @return The item, or empty if none arrived in time.
         */
        [[nodiscard]] auto Pop(std::chrono::milliseconds timeout) -> std::optional<T> {
            std::unique_lock lock{ m_mutex };
            if (!m_ready.wait_for(lock, timeout, [this] { return !m_items.empty(); })) {
                return std::nullopt;
            }
            auto item{ std::move(m_items.front()) };
            m_items.pop_front();
            return item;
        }
    };

} // namespace test_support
