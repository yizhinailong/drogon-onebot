/**
 * @file action_call.cpp
 * @brief Implement the coroutines that call an action on a shared transport.
 */
#include "details/action_call.hpp"

#include <expected>
#include <memory>
#include <string>
#include <utility>

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
    auto call(
        std::shared_ptr<Transport const> transport,
        std::string action,
        Json::Value params,
        double timeout
    ) -> drogon::Task<Result<Json::Value>> {
        co_return co_await transport->Call(std::move(action), std::move(params), timeout);
    }

    /**
     * @brief Call an action whose `data` carries nothing to read.
     * @param transport Transport to call on; the task keeps it alive until it completes.
     * @param action Action name, e.g. `set_group_kick`.
     * @param params Action parameters, sent as the JSON request body.
     * @param timeout Seconds to wait for the response; 0 waits forever.
     * @return Task yielding nothing once the action succeeded, or why it failed.
     */
    auto call_and_discard(
        std::shared_ptr<Transport const> transport,
        std::string action,
        Json::Value params,
        double timeout
    ) -> drogon::Task<Result<void>> {
        auto const data{ co_await transport->Call(std::move(action), std::move(params), timeout) };
        if (!data) {
            co_return std::unexpected{ data.error() };
        }
        co_return Result<void>{};
    }

} // namespace drogon_onebot::details
