#include <numeraire/utils/log_level.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace numeraire::utils {
namespace {

[[nodiscard]] std::string ToLowerAscii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

[[nodiscard]] std::string_view TrimAscii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

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
