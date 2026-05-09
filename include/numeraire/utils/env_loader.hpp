#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace numeraire::utils {

/// Loads `KEY=value` pairs from a dotenv-style file (`.env`).
///
/// This is a plain text format — not an OS feature. Values are stored in an
/// in-memory map until merged into the process environment (see
/// ApplyToEnvironment).
///
/// Precedence for Get(): a non-empty `std::getenv(key)` wins over the file map
/// ("real environment wins over .env").
class EnvLoader {
public:
    /// Parses `path`. If the file does not exist, this is a no-op.
    /// Throws numeraire::ValidationError on malformed non-empty lines that
    /// are not comments and contain no `=` separator.
    void LoadFromFile(const std::filesystem::path& path);

    /// Clears all keys previously loaded from files (does not modify the OS
    /// environment).
    void Clear() noexcept { entries_.clear(); }

    /// Returns getenv(key) when it is non-empty; otherwise the value from the
    /// last successful LoadFromFile; otherwise std::nullopt.
    [[nodiscard]] std::optional<std::string> Get(std::string_view key) const;

    /// For each (key, value) in the in-memory map: if getenv(key) is absent or
    /// empty, calls POSIX setenv so legacy code that only uses getenv() (e.g.
    /// Logger::Init) observes .env values. No-op on Windows.
    void ApplyToEnvironment() const;

private:
    std::unordered_map<std::string, std::string> entries_;
};

}  // namespace numeraire::utils
