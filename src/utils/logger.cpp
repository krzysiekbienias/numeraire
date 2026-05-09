#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <numeraire/utils/logger.hpp>
#include <stdexcept>
#include <vector>

namespace numeraire::utils {
namespace {

[[nodiscard]] spdlog::level::level_enum ToSpdlog(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::kTrace:
        return spdlog::level::trace;
    case LogLevel::kDebug:
        return spdlog::level::debug;
    case LogLevel::kInfo:
        return spdlog::level::info;
    case LogLevel::kWarn:
        return spdlog::level::warn;
    case LogLevel::kError:
        return spdlog::level::err;
    case LogLevel::kCritical:
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

}  // namespace

void Logger::Init(std::optional<LogLevel> level, std::optional<std::filesystem::path> log_file) {
    LogLevel resolved = LogLevel::kInfo;
    if (level.has_value()) {
        resolved = *level;
    } else if (const auto from_env = LogLevelFromEnvironment(); from_env.has_value()) {
        resolved = *from_env;
    }

    spdlog::drop("numeraire");

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (log_file.has_value()) {
        const std::filesystem::path& path = *log_file;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        constexpr std::size_t kMaxFileBytes = 10 * 1024 * 1024;
        constexpr std::size_t kMaxRotatedFiles = 5;
        sinks.push_back(
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path.string(), kMaxFileBytes, kMaxRotatedFiles));
    }

    auto logger = std::make_shared<spdlog::logger>("numeraire", sinks.begin(), sinks.end());
    logger->set_level(ToSpdlog(resolved));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    logger->flush_on(spdlog::level::warn);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
}

void Logger::Shutdown() noexcept {
    spdlog::shutdown();
}

spdlog::logger& Logger::Core() {
    std::shared_ptr<spdlog::logger> logger = spdlog::get("numeraire");
    if (logger == nullptr) {
        throw std::logic_error("Logger::Init has not been called");
    }
    return *logger;
}

}  // namespace numeraire::utils
