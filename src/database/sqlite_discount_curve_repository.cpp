#include <numeraire/database/sqlite_discount_curve_repository.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

namespace numeraire::database {

SqliteDiscountCurveRepository::SqliteDiscountCurveRepository(std::string database_file_path)
    : database_file_path_(std::move(database_file_path)) {}

void SqliteDiscountCurveRepository::UpsertCurve(const DiscountCurveEodHeaderWrite& header,
                                                const std::vector<DiscountCurvePointWrite>& points) {
    try {
        SQLite::Database db(database_file_path_, SQLite::OPEN_READWRITE);
        db.exec("PRAGMA foreign_keys = ON");
        SQLite::Transaction txn(db);

        SQLite::Statement upsert_header(
                db,
                "INSERT INTO discount_curve_eod ("
                "curve_id, as_of, source_par_curve_id, source_par_as_of, currency, day_count, "
                "session_calendar, interpolation_method, bootstrap_engine, batch_run_id, ingested_at"
                ") VALUES (?,?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT (curve_id, as_of) DO UPDATE SET "
                "source_par_curve_id = excluded.source_par_curve_id, "
                "source_par_as_of = excluded.source_par_as_of, "
                "currency = excluded.currency, "
                "day_count = excluded.day_count, "
                "session_calendar = excluded.session_calendar, "
                "interpolation_method = excluded.interpolation_method, "
                "bootstrap_engine = excluded.bootstrap_engine, "
                "batch_run_id = excluded.batch_run_id, "
                "ingested_at = excluded.ingested_at");
        upsert_header.bind(1, header.curve_id);
        upsert_header.bind(2, header.as_of);
        upsert_header.bind(3, header.source_par_curve_id);
        upsert_header.bind(4, header.source_par_as_of);
        upsert_header.bind(5, header.currency);
        upsert_header.bind(6, header.day_count);
        upsert_header.bind(7, header.session_calendar);
        upsert_header.bind(8, header.interpolation_method);
        upsert_header.bind(9, header.bootstrap_engine);
        upsert_header.bind(10, header.batch_run_id);
        upsert_header.bind(11, header.ingested_at);
        upsert_header.exec();

        SQLite::Statement del(
                db, "DELETE FROM discount_curve_point_eod WHERE curve_id = ? AND as_of = ?");
        del.bind(1, header.curve_id);
        del.bind(2, header.as_of);
        del.exec();

        SQLite::Statement ins(
                db,
                "INSERT INTO discount_curve_point_eod (curve_id, as_of, tenor, time_years, zero_rate, "
                "discount_factor) VALUES (?,?,?,?,?,?)");
        for (const DiscountCurvePointWrite& point : points) {
            ins.bind(1, header.curve_id);
            ins.bind(2, header.as_of);
            ins.bind(3, point.tenor);
            ins.bind(4, point.time_years);
            ins.bind(5, point.zero_rate);
            ins.bind(6, point.discount_factor);
            ins.exec();
            ins.reset();
        }

        txn.commit();
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteDiscountCurveRepository::UpsertCurve: "} + e.what());
    }
}

}  // namespace numeraire::database
