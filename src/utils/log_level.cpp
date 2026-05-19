#include <cstdlib>
#include <cstring>
#include <numeraire/utils/log_level.hpp>
#include <numeraire/utils/string.hpp>

namespace numeraire::utils {

std::optional<LogLevel> TryParseLogLevel(std::string_view text) noexcept {
    const std::string lowered = ToLowerAscii(TrimAscii(text));
    if (lowered.empty()) {
        return std::nullopt;
    }
    if (lowered == "trace") {
        return LogLevel::kTrace;
    }
    if (lowered == "debug") {
        return LogLevel::kDebug;
    }
    if (lowered == "info") {
        return LogLevel::kInfo;
    }
    if (lowered == "warn" || lowered == "warning") {
        return LogLevel::kWarn;
    }
    if (lowered == "error") {
        return LogLevel::kError;
    }
    if (lowered == "critical" || lowered == "fatal") {
        return LogLevel::kCritical;
    }
    return std::nullopt;
}

std::optional<LogLevel> LogLevelFromEnvironment() noexcept {
    const char* raw = std::getenv("NUMERAIRE_LOG_LEVEL");
    if (raw == nullptr || std::strlen(raw) == 0) {
        return std::nullopt;
    }
    return TryParseLogLevel(raw);
}

}  // namespace numeraire::utils
