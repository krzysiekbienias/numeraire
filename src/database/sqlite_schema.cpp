#include <numeraire/database/sqlite_schema.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <fstream>
#include <sstream>
#include <string>

namespace numeraire::database {
namespace {

[[nodiscard]] std::string ReadEntireFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw PersistenceError("BootstrapTradeDatabaseSchema: cannot open schema file: " + path.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

}  // namespace

void BootstrapTradeDatabaseSchema(const std::filesystem::path& database_path,
                                  const std::filesystem::path& schema_sql_path) {
    const std::filesystem::path parent = database_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const std::string sql = ReadEntireFile(schema_sql_path);

    try {
        SQLite::Database db(database_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("PRAGMA foreign_keys = ON;");
        db.exec(sql);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"BootstrapTradeDatabaseSchema: "} + e.what());
    }
}

}  // namespace numeraire::database
