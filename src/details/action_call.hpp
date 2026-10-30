/**
 * @file action_call.hpp
 * @brief Declare the coroutines that the `OneBot` methods return: they call an action on a shared transport and read its `data`.
 */
#pragma once

#include <expected>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include "details/json.hpp"
#include "details/transport.hpp"
#include "drogon_onebot/error.hpp"

namespace drogon_onebot::details {

    /**
     * @brief Call an action and await the `data` of its response envelope.
     *
     * `transport` is a parameter, so it enters the coroutine frame when the coroutine is
     * called. `drogon::Task` runs its body only once awaited, so a copy made there would
     * read a `OneBot` that the caller may already have destroyed.
     * @param transport Transport to call on; the task keeps it alive until it completes.
     * @param action Action name, e.g. `send_private_msg`.
     * @param params Action parameters, sent as the JSON request body.
     * @param timeout Seconds to wait for the response; 0 waits forever.
     * @return Task yielding the envelope's `data` (null for an `async` response), or why the action failed.
     */
    [[nodiscard]] auto call(
        std::shared_ptr<Transport> transport,
        std::string action,
        Json::Value params,
        double timeout
    ) -> drogon::Task<Result<Json::Value>>;

    /**
     * @brief Call an action whose `data` carries nothing to read.
     * @param transport Transport to call on; the task keeps it alive until it completes.
     * @param action Action name, e.g. `set_group_kick`.
     * @param params Action parameters, sent as the JSON request body.
     * @param timeout Seconds to wait for the response; 0 waits forever.
     * @return Task yielding nothing once the action succeeded, or why it failed.
     */
    [[nodiscard]] auto call_and_discard(
        std::shared_ptr<Transport> transport,
        std::string action,
        Json::Value params,
        double timeout
    ) -> drogon::Task<Result<void>>;

    /**
     * @brief Call an action and read its `data` into `T`.
     * @tparam T Value type of the action's result.
     * @tparam Read Callable that reads `data` into `T` or describes its first problem.
     * @param transport Transport to call on; the task keeps it alive until it completes.
     * @param action Action name, e.g. `get_group_info`; also names the action in a `ResponseValidation` error.
     * @param params Action parameters, sent as the JSON request body.
     * @param timeout Seconds to wait for the response; 0 waits forever.
     * @param read Reader of `data`, e.g. `to_group_info`.
     * @return Task yielding the value, or why the action failed: a `ResponseValidation`
     * error that carries `data` when `read` finds a problem.
     */
    template <typename T, typename Read>
    requires std::is_invocable_r_v<std::expected<T, std::string>, Read&, Json::Value const&>
    [[nodiscard]] auto call_and_read(
        std::shared_ptr<Transport> transport,
        std::string action,
        Json::Value params,
        double timeout,
        Read read
    ) -> drogon::Task<Result<T>> {
        auto const data{ co_await transport->Call(action, std::move(params), timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        std::expected<T, std::string> value{ read(*data) };
        if (!value) {
            co_return std::unexpected{ invalid_data_error(action, value.error(), *data) };
        }
        co_return std::move(*value);
    }

} // namespace drogon_onebot::details
