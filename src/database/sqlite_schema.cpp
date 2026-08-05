#include <numeraire/database/sqlite_schema.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <fstream>
#include <sstream>
#include <string>

namespace numeraire::database {
namespace {

void ApplySchemaPatches(SQLite::Database& db) {
    {
        SQLite::Statement columns(
                db,
                "SELECT 1 FROM pragma_table_info('par_curve_point_eod') WHERE name = 'quoted_price'");
        if (!columns.executeStep()) {
            db.exec("ALTER TABLE par_curve_point_eod ADD COLUMN quoted_price REAL");
        }
    }
    // Rename legacy PFE column (was mislabeled pfe_97; stores the 97.5% quantile).
    for (const char* table : {"trade_leg_exposure_eod", "trade_leg_exposure_eod_archive"}) {
        SQLite::Statement has_old(
                db, std::string{"SELECT 1 FROM pragma_table_info('"} + table + "') WHERE name = 'pfe_97'");
        SQLite::Statement has_new(
                db, std::string{"SELECT 1 FROM pragma_table_info('"} + table + "') WHERE name = 'pfe_975'");
        if (has_old.executeStep() && !has_new.executeStep()) {
            db.exec(std::string{"ALTER TABLE "} + table + " RENAME COLUMN pfe_97 TO pfe_975");
        }
    }
    // Multi-engine marks: flag the one that feeds reporting, plus Monte Carlo inputs
    // needed to reproduce a historical valuation. `CREATE TABLE IF NOT EXISTS` in
    // schema_v1.sql cannot add these to databases that predate them.
    for (const char* table : {"trade_leg_mtm_eod", "trade_leg_mtm_eod_archive"}) {
        const std::string has_column_sql =
                std::string{"SELECT 1 FROM pragma_table_info('"} + table + "') WHERE name = ?";

        SQLite::Statement has_official(db, has_column_sql);
        has_official.bind(1, "is_official");
        if (!has_official.executeStep()) {
            db.exec(std::string{"ALTER TABLE "} + table +
                    " ADD COLUMN is_official INTEGER NOT NULL DEFAULT 0 CHECK (is_official IN (0, 1))");
            // Every mark written before this patch came from the single analytic engine,
            // so all of them were official. Runs once, in the same step that adds the column.
            db.exec(std::string{"UPDATE "} + table + " SET is_official = 1");
        }

        for (const char* column : {"num_paths", "mc_seed"}) {
            SQLite::Statement has_column(db, has_column_sql);
            has_column.bind(1, column);
            if (!has_column.executeStep()) {
                db.exec(std::string{"ALTER TABLE "} + table + " ADD COLUMN " + column + " INTEGER");
            }
        }
    }
    // Only on the live table: the archive keeps every run, each with its own official mark.
    db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_trade_leg_mtm_eod_official "
            "ON trade_leg_mtm_eod (leg_id, as_of) WHERE is_official = 1");
}

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
        ApplySchemaPatches(db);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"BootstrapTradeDatabaseSchema: "} + e.what());
    }
}

}  // namespace numeraire::database
