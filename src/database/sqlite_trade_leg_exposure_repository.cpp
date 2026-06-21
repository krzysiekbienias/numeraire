#include <SQLiteCpp/SQLiteCpp.h>

#include <array>
#include <chrono>
#include <memory>
#include <numeraire/database/sqlite_trade_leg_exposure_repository.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>

namespace numeraire::database {

namespace {

[[nodiscard]] std::string DefaultUtcTimestampIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::array<char, 32> buf{};
    if (std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) == 0U) {
        return "1970-01-01T00:00:00Z";
    }
    return std::string(buf.data());
}

constexpr const char* kArchiveInsertSql =
        "INSERT INTO trade_leg_exposure_eod_archive ("
        "batch_run_id, calculated_at, as_of, trade_id, leg_id, pillar_id, grid_step, "
        "year_fraction, exposure_date, ee, pfe_95, pfe_97, num_paths, mc_seed, calibration_id, "
        "scope_key, pricing_engine, remarks"
        ") VALUES ("
        "?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?"
        ")";

constexpr const char* kUpsertSql =
        "INSERT OR REPLACE INTO trade_leg_exposure_eod ("
        "as_of, trade_id, leg_id, pillar_id, grid_step, year_fraction, exposure_date, "
        "ee, pfe_95, pfe_97, num_paths, mc_seed, calibration_id, scope_key, batch_run_id, "
        "pricing_engine, calculated_at, remarks"
        ") VALUES ("
        "?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?"
        ")";

}  // namespace

struct SqliteTradeLegExposureRepository::Impl {
    std::unique_ptr<SQLite::Database> db;
    std::unique_ptr<SQLite::Statement> archive_insert;
    std::unique_ptr<SQLite::Statement> upsert;
};

SqliteTradeLegExposureRepository::SqliteTradeLegExposureRepository(const std::string& database_file_path)
    : impl_(std::make_unique<Impl>()) {
    try {
        impl_->db =
                std::make_unique<SQLite::Database>(database_file_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        impl_->db->exec("PRAGMA foreign_keys = ON;");
        impl_->archive_insert = std::make_unique<SQLite::Statement>(*impl_->db, kArchiveInsertSql);
        impl_->upsert = std::make_unique<SQLite::Statement>(*impl_->db, kUpsertSql);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegExposureRepository: "} + e.what());
    }
}

SqliteTradeLegExposureRepository::~SqliteTradeLegExposureRepository() = default;

void SqliteTradeLegExposureRepository::InsertArchive(const TradeLegExposureEodRow& row,
                                                     const std::string& calculated_at) const {
    if (!row.batch_run_id.has_value() || row.batch_run_id->empty()) {
        throw ValidationError("TradeLegExposureEodRow: batch_run_id is required for archive insert");
    }

    try {
        SQLite::Statement& st = *impl_->archive_insert;
        st.reset();
        st.clearBindings();

        int i = 1;
        st.bind(i++, *row.batch_run_id);
        st.bind(i++, calculated_at);
        st.bind(i++, row.as_of);
        st.bind(i++, row.trade_id);
        st.bind(i++, row.leg_id);
        st.bind(i++, row.pillar_id);
        st.bind(i++, row.grid_step);
        st.bind(i++, row.year_fraction);
        st.bind(i++, row.exposure_date);
        st.bind(i++, row.ee);
        st.bind(i++, row.pfe_95);
        st.bind(i++, row.pfe_97);
        st.bind(i++, row.num_paths);
        st.bind(i++, row.mc_seed);
        if (row.calibration_id.has_value()) {
            st.bind(i++, *row.calibration_id);
        } else {
            st.bind(i++);
        }
        st.bind(i++, row.scope_key);
        st.bind(i++, row.pricing_engine);
        st.bind(i++, row.remarks);

        st.exec();
    } catch (ValidationError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegExposureRepository::InsertArchive: "} + e.what());
    }
}

void SqliteTradeLegExposureRepository::Upsert(const TradeLegExposureEodRow& row) const {
    if (row.as_of.empty() || row.trade_id.empty() || row.leg_id.empty() || row.pillar_id.empty() ||
        row.pricing_engine.empty() || row.scope_key.empty() || row.exposure_date.empty()) {
        throw ValidationError(
                "TradeLegExposureEodRow: as_of, trade_id, leg_id, pillar_id, exposure_date, scope_key, and "
                "pricing_engine must be non-empty");
    }
    if (row.num_paths <= 0) {
        throw ValidationError("TradeLegExposureEodRow: num_paths must be > 0");
    }

    const std::string calculated_at = row.calculated_at.empty() ? DefaultUtcTimestampIso8601() : row.calculated_at;

    if (row.batch_run_id.has_value() && !row.batch_run_id->empty()) {
        InsertArchive(row, calculated_at);
    }

    try {
        SQLite::Statement& st = *impl_->upsert;
        st.reset();
        st.clearBindings();

        int i = 1;
        st.bind(i++, row.as_of);
        st.bind(i++, row.trade_id);
        st.bind(i++, row.leg_id);
        st.bind(i++, row.pillar_id);
        st.bind(i++, row.grid_step);
        st.bind(i++, row.year_fraction);
        st.bind(i++, row.exposure_date);
        st.bind(i++, row.ee);
        st.bind(i++, row.pfe_95);
        st.bind(i++, row.pfe_97);
        st.bind(i++, row.num_paths);
        st.bind(i++, row.mc_seed);
        if (row.calibration_id.has_value()) {
            st.bind(i++, *row.calibration_id);
        } else {
            st.bind(i++);
        }
        st.bind(i++, row.scope_key);
        if (row.batch_run_id.has_value() && !row.batch_run_id->empty()) {
            st.bind(i++, *row.batch_run_id);
        } else {
            st.bind(i++);
        }
        st.bind(i++, row.pricing_engine);
        st.bind(i++, calculated_at);
        st.bind(i++, row.remarks);

        st.exec();
    } catch (ValidationError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegExposureRepository::Upsert: "} + e.what());
    }
}

}  // namespace numeraire::database
