/**
 * @file error.hpp
 * @brief Declare the error type that failed actions return through `Result`.
 */
#pragma once

#include <expected>
#include <string>

namespace drogon_onebot {

    /**
     * @brief Category of a failed action.
     */
    enum class ErrorKind {
        Connection,         ///< No response arrived, e.g. DNS, TLS, or network failure.
        Timeout,            ///< No response arrived within the timeout.
        Status,             ///< The OneBot implementation answered with a non-2xx status.
        ResponseValidation, ///< A 2xx response was not a OneBot response envelope of the expected shape.
        Action,             ///< The OneBot implementation answered with `"status": "failed"`.
    };

    /**
     * @brief Describe why an action failed.
     */
    struct Error {
        ErrorKind kind;      ///< Failure category.
        std::string message; ///< Human-readable description; includes the envelope's `message` or `wording` when present.
        int status_code{};   ///< HTTP status for `ErrorKind::Status`; 0 otherwise.
        int retcode{};       ///< OneBot `retcode` for `ErrorKind::Action`; 0 otherwise.
        std::string body;    ///< Response body for `Status`, `ResponseValidation`, and `Action`; empty otherwise.
    };

    /**
     * @brief Value of a successful action, or the error that prevented it.
     * @tparam T Value type on success.
     */
    template <typename T>
    using Result = std::expected<T, Error>;

} // namespace drogon_onebot
