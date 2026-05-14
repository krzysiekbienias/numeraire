#pragma once

#include <filesystem>

namespace numeraire::database {

/// Opens (creates if needed) `database_path`, ensures parent directories exist,
/// and executes the full SQL script from `schema_sql_path` (e.g.
/// `sql/schema_v1.sql`). Safe to call repeatedly (`CREATE TABLE IF NOT EXISTS`).
void BootstrapTradeDatabaseSchema(const std::filesystem::path& database_path,
                                    const std::filesystem::path& schema_sql_path);

}  // namespace numeraire::database
