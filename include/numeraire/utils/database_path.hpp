#pragma once

#include <filesystem>

namespace numeraire::utils {

class Config;

/// Resolved SQLite trade-store path: non-empty `NUMERAIRE_DB_PATH` from the
/// environment wins (after `EnvLoader::ApplyToEnvironment()`); otherwise
/// `database.path` from JSON config (`RequireString`).
[[nodiscard]] std::filesystem::path ResolveDatabasePath(const Config& cfg);

}  // namespace numeraire::utils
