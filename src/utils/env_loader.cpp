#include <cctype>
#include <cstdlib>
#include <fstream>
#include <numeraire/utils/env_loader.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>

namespace numeraire::utils {
namespace {

[[nodiscard]] std::string_view TrimAscii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool IsCommentOrEmpty(std::string_view line) {
    const std::string_view t = TrimAscii(line);
    return t.empty() || t.front() == '#';
}

}  // namespace

void EnvLoader::LoadFromFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream in(path);
    if (!in) {
        throw numeraire::ConfigError("EnvLoader: cannot open file: " + path.string());
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        const std::string_view raw = TrimAscii(line);
        if (IsCommentOrEmpty(raw)) {
            continue;
        }
        const std::size_t eq = raw.find('=');
        if (eq == std::string_view::npos) {
            throw numeraire::ValidationError("EnvLoader: line " + std::to_string(line_number) +
                                             " has no '=' separator");
        }
        std::string key = std::string(TrimAscii(raw.substr(0, eq)));
        std::string value = std::string(TrimAscii(raw.substr(eq + 1)));
        if (key.empty()) {
            throw numeraire::ValidationError("EnvLoader: line " + std::to_string(line_number) + " has an empty key");
        }
        entries_[std::move(key)] = std::move(value);
    }
}

std::optional<std::string> EnvLoader::Get(std::string_view key) const {
    const std::string key_string(key);
    if (const char* from_os = std::getenv(key_string.c_str()); from_os != nullptr && from_os[0] != '\0') {
        return std::string(from_os);
    }
    const auto it = entries_.find(key_string);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void EnvLoader::ApplyToEnvironment() const {
#if defined(_WIN32)
    (void)entries_;
    return;
#else
    for (const auto& [key, value] : entries_) {
        const char* current = std::getenv(key.c_str());
        if (current != nullptr && current[0] != '\0') {
            continue;
        }
        if (::setenv(key.c_str(), value.c_str(), 1) != 0) {
            throw numeraire::ConfigError("EnvLoader: setenv failed for key: " + key);
        }
    }
#endif
}

}  // namespace numeraire::utils
