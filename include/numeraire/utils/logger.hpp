#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>
#include <numeraire/utils/log_level.hpp>
#include <optional>

namespace numeraire::utils {

/// Central spdlog facade. Call Init() once near process entry; use the NUM_*
/// macros for logging.
class Logger {
public:
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
    Logger() = delete;

    template <typename... Args>
    void NumTrace(fmt::format_string<Args...> fmt, Args&&... args) {
        Logger::Core().trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void NumDebug(fmt::format_string<Args...> fmt, Args&&... args) {
        Logger::Core().debug(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void NumInfo(fmt::format_string<Args...> fmt, Args&&... args) {
        Logger::Core().info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void NumWarn(fmt::format_string<Args...> fmt, Args&&... args) {
        Logger::Core().warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void NumError(fmt::format_string<Args...> fmt, Args&&... args) {
        Logger::Core().error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void NumCritical(fmt::format_string<Args...> fmt, Args&&... args) {
        Logger::Core().critical(fmt, std::forward<Args>(args)...);
    }
};

}  // namespace numeraire::utils
