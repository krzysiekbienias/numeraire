#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace numeraire::utils {

/// Severity levels for the shared application logger (spdlog-backed).
enum class LogLevel {
    kTrace,
    kDebug,
    kInfo,
    kWarn,
    kError,
    kCritical,
};

/// Parses a case-insensitive string ("info", "INFO", …) into LogLevel.
/// Returns std::nullopt if the string is not recognized.
[[nodiscard]] std::optional<LogLevel> TryParseLogLevel(std::string_view text) noexcept;

/// Reads `NUMERAIRE_LOG_LEVEL` from the environment (if set) and parses it.
/// Returns std::nullopt when the variable is unset or not recognized.
[[nodiscard]] std::optional<LogLevel> LogLevelFromEnvironment() noexcept;

}  // namespace numeraire::utils
