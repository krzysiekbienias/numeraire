#pragma once

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <numeraire/utils/log_level.hpp>
#include <optional>
#include <utility>

namespace numeraire::utils {

/// Central spdlog facade. Call Init() once near process entry; use NumInfo /
/// NumDebug / … for type-safe logging.
class Logger {
public:
    Logger() = delete;

    /// Initializes sinks and the default logger named "numeraire".
    ///
    /// When `level` is std::nullopt, the level is taken from the environment
    /// variable `NUMERAIRE_LOG_LEVEL` if set and valid; otherwise kInfo.
    ///
    /// When `log_file` is set, logs are also written to a rotating file sink.
    static void Init(std::optional<LogLevel> level = std::nullopt,
                     std::optional<std::filesystem::path> log_file = std::nullopt);

    /// Flushes and drops registered loggers so a subsequent Init() is safe
    /// (used heavily in unit tests).
    static void Shutdown() noexcept;

    /// Returns the shared core logger instance. Init() must have run first.
    static spdlog::logger& Core();

    template <typename... Args>
    static void NumTrace(fmt::format_string<Args...> fmt, Args&&... args) {
        Core().trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void NumDebug(fmt::format_string<Args...> fmt, Args&&... args) {
        Core().debug(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void NumInfo(fmt::format_string<Args...> fmt, Args&&... args) {
        Core().info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void NumWarn(fmt::format_string<Args...> fmt, Args&&... args) {
        Core().warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void NumError(fmt::format_string<Args...> fmt, Args&&... args) {
        Core().error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void NumCritical(fmt::format_string<Args...> fmt, Args&&... args) {
        Core().critical(fmt, std::forward<Args>(args)...);
    }
};

}  // namespace numeraire::utils
