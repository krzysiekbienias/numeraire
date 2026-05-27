#include <numeraire/database/sqlite_vol_surface_repository.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

namespace numeraire::database {

SqliteVolSurfaceRepository::SqliteVolSurfaceRepository(std::string database_file_path)
    : database_file_path_(std::move(database_file_path)) {}

long SqliteVolSurfaceRepository::UpsertSurface(const VolSurfaceEodHeaderWrite& header,
                                               const std::vector<VolSurfacePointWrite>& points) {
    try {
        SQLite::Database db(database_file_path_, SQLite::OPEN_READWRITE);
        SQLite::Transaction txn(db);

        long surface_id = 0;
        {
            SQLite::Statement find(
                    db,
                    "SELECT surface_id FROM vol_surface_eod WHERE underlying_id = ? AND as_of = ? AND surface_kind = ?");
            find.bind(1, header.underlying_id);
            find.bind(2, header.as_of);
            find.bind(3, header.surface_kind);
            if (find.executeStep()) {
                surface_id = find.getColumn(0).getInt64();
                SQLite::Statement upd(
                        db,
                        "UPDATE vol_surface_eod SET coordinate_system = ?, spot_used = ?, risk_free_rate = ?, "
                        "dividend_yield = ?, model = ?, price_source = ?, currency = ?, point_count = ?, "
                        "ingested_at = ?, batch_run_id = ? WHERE surface_id = ?");
                upd.bind(1, header.coordinate_system);
                upd.bind(2, header.spot_used);
                upd.bind(3, header.risk_free_rate);
                upd.bind(4, header.dividend_yield);
                upd.bind(5, header.model);
                upd.bind(6, header.price_source);
                upd.bind(7, header.currency);
                upd.bind(8, static_cast<int>(points.size()));
                upd.bind(9, header.ingested_at);
                upd.bind(10, header.batch_run_id);
                upd.bind(11, surface_id);
                upd.exec();

                SQLite::Statement del(db, "DELETE FROM vol_surface_point_eod WHERE surface_id = ?");
                del.bind(1, surface_id);
                del.exec();
            } else {
                SQLite::Statement ins(
                        db,
                        "INSERT INTO vol_surface_eod (underlying_id, as_of, surface_kind, coordinate_system, "
                        "spot_used, risk_free_rate, dividend_yield, model, price_source, currency, point_count, "
                        "ingested_at, batch_run_id) "
                        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
                ins.bind(1, header.underlying_id);
                ins.bind(2, header.as_of);
                ins.bind(3, header.surface_kind);
                ins.bind(4, header.coordinate_system);
                ins.bind(5, header.spot_used);
                ins.bind(6, header.risk_free_rate);
                ins.bind(7, header.dividend_yield);
                ins.bind(8, header.model);
                ins.bind(9, header.price_source);
                ins.bind(10, header.currency);
                ins.bind(11, static_cast<int>(points.size()));
                ins.bind(12, header.ingested_at);
                ins.bind(13, header.batch_run_id);
                ins.exec();
                surface_id = db.getLastInsertRowid();
            }
        }

        SQLite::Statement pt(
                db,
                "INSERT INTO vol_surface_point_eod (surface_id, expiration_date, years_to_maturity, strike, "
                "contract_type, implied_vol, source_option_ticker, input_price, quality) "
                "VALUES (?,?,?,?,?,?,?,?,?)");
        for (const VolSurfacePointWrite& p : points) {
            pt.bind(1, surface_id);
            pt.bind(2, p.expiration_date);
            pt.bind(3, p.years_to_maturity);
            pt.bind(4, p.strike);
            pt.bind(5, p.contract_type);
            pt.bind(6, p.implied_vol);
            pt.bind(7, p.source_option_ticker);
            pt.bind(8, p.input_price);
            pt.bind(9, p.quality);
            pt.exec();
            pt.reset();
        }

        txn.commit();
        return surface_id;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteVolSurfaceRepository::UpsertSurface: "} + e.what());
    }
}

}  // namespace numeraire::database
