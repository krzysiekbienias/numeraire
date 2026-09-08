#include <numeraire/database/futures_pillar_returns.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace numeraire::database {
namespace {

/// One priced, unsettled contract on one session.
struct ContractQuote {
    std::string ticker;
    double price{0.0};
};

/// Sessions in ascending date order, each holding its unsettled contracts sorted
/// by settlement date, so index 0 is the front month.
struct SessionCurve {
    std::string as_of;
    std::vector<ContractQuote> by_maturity;
};

/// `futures_contract` is keyed by (ticker, listing_as_of); the newest listing row
/// per ticker carries the settlement date we rank on.
constexpr const char* kCurveSql =
        "WITH contract_meta AS ("
        "  SELECT ticker, settlement_date,"
        "         ROW_NUMBER() OVER (PARTITION BY ticker ORDER BY listing_as_of DESC) AS rn"
        "  FROM futures_contract"
        "  WHERE product_code = ? AND settlement_date IS NOT NULL"
        ") "
        "SELECT e.as_of, e.ticker, COALESCE(e.settlement_price, e.close) AS px "
        "FROM futures_daily_eod e "
        "JOIN contract_meta m ON m.ticker = e.ticker AND m.rn = 1 "
        "WHERE e.as_of >= ? AND e.as_of <= ? "
        "  AND m.settlement_date > e.as_of "
        "  AND COALESCE(e.settlement_price, e.close) > 0 "
        "ORDER BY e.as_of ASC, m.settlement_date ASC";

/// The same ranking as `kCurveSql`, narrowed to one session and carrying the
/// settlement date so callers can read each pillar's maturity.
constexpr const char* kOccupantSql =
        "WITH contract_meta AS ("
        "  SELECT ticker, settlement_date,"
        "         ROW_NUMBER() OVER (PARTITION BY ticker ORDER BY listing_as_of DESC) AS rn"
        "  FROM futures_contract"
        "  WHERE product_code = ? AND settlement_date IS NOT NULL"
        ") "
        "SELECT e.ticker, m.settlement_date, COALESCE(e.settlement_price, e.close) AS px "
        "FROM futures_daily_eod e "
        "JOIN contract_meta m ON m.ticker = e.ticker AND m.rn = 1 "
        "WHERE e.as_of = ? "
        "  AND m.settlement_date > e.as_of "
        "  AND COALESCE(e.settlement_price, e.close) > 0 "
        "ORDER BY m.settlement_date ASC";

/// Latest quoted session at or before the requested date; same filters as occupancy.
constexpr const char* kLatestOccupantSessionSql =
        "WITH contract_meta AS ("
        "  SELECT ticker, settlement_date,"
        "         ROW_NUMBER() OVER (PARTITION BY ticker ORDER BY listing_as_of DESC) AS rn"
        "  FROM futures_contract"
        "  WHERE product_code = ? AND settlement_date IS NOT NULL"
        ") "
        "SELECT MAX(e.as_of) "
        "FROM futures_daily_eod e "
        "JOIN contract_meta m ON m.ticker = e.ticker AND m.rn = 1 "
        "WHERE e.as_of <= ? "
        "  AND m.settlement_date > e.as_of "
        "  AND COALESCE(e.settlement_price, e.close) > 0";

[[nodiscard]] std::string QuoteKey(const std::string_view ticker, const std::string_view as_of) {
    std::string key;
    key.reserve(ticker.size() + as_of.size() + 1U);
    key.append(ticker);
    key.push_back('|');
    key.append(as_of);
    return key;
}

[[nodiscard]] std::vector<SessionCurve> LoadSessionCurves(SQLite::Database& db,
                                                          const std::string_view product_code,
                                                          const std::string_view from_iso,
                                                          const std::string_view to_iso,
                                                          std::unordered_map<std::string, double>& price_by_key) {
    SQLite::Statement st(db, kCurveSql);
    st.bind(1, std::string(product_code));
    st.bind(2, std::string(from_iso));
    st.bind(3, std::string(to_iso));

    std::vector<SessionCurve> sessions;
    while (st.executeStep()) {
        std::string as_of = st.getColumn(0).getString();
        ContractQuote quote{
                .ticker = st.getColumn(1).getString(),
                .price = st.getColumn(2).getDouble(),
        };
        price_by_key.emplace(QuoteKey(quote.ticker, as_of), quote.price);

        if (sessions.empty() || sessions.back().as_of != as_of) {
            sessions.push_back(SessionCurve{.as_of = std::move(as_of), .by_maturity = {}});
        }
        sessions.back().by_maturity.push_back(std::move(quote));
    }
    return sessions;
}

[[nodiscard]] std::vector<FuturesPillarOccupant> LoadOccupantsOnSession(SQLite::Database& db,
                                                                        const std::string_view product_code,
                                                                        const std::string_view as_of_iso,
                                                                        const int num_pillars) {
    SQLite::Statement st(db, kOccupantSql);
    st.bind(1, std::string(product_code));
    st.bind(2, std::string(as_of_iso));

    std::vector<FuturesPillarOccupant> out;
    int pillar = 0;
    while (st.executeStep() && pillar < num_pillars) {
        ++pillar;
        out.push_back(FuturesPillarOccupant{
                .pillar = pillar,
                .factor_id = FuturesPillarFactorId(product_code, pillar),
                .ticker = st.getColumn(0).getString(),
                .settlement_date = st.getColumn(1).getString(),
        });
    }
    return out;
}

}  // namespace

std::string FuturesPillarFactorId(const std::string_view product_code, const int pillar) {
    return std::string(product_code) + "_M" + std::to_string(pillar);
}

std::vector<FuturesPillarSeries> LoadFuturesPillarReturns(const std::string& database_file_path,
                                                          const std::string_view product_code,
                                                          const std::string_view from_iso_yyyy_mm_dd,
                                                          const std::string_view to_iso_yyyy_mm_dd,
                                                          const int num_pillars) {
    if (num_pillars <= 0) {
        throw ValidationError("LoadFuturesPillarReturns: num_pillars must be > 0.");
    }

    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);

        std::unordered_map<std::string, double> price_by_key;
        const std::vector<SessionCurve> sessions =
                LoadSessionCurves(db, product_code, from_iso_yyyy_mm_dd, to_iso_yyyy_mm_dd, price_by_key);

        std::vector<FuturesPillarSeries> out;
        out.reserve(static_cast<std::size_t>(num_pillars));

        for (int pillar = 1; pillar <= num_pillars; ++pillar) {
            const auto rank = static_cast<std::size_t>(pillar - 1);
            FuturesPillarSeries series{};
            series.factor_id = FuturesPillarFactorId(product_code, pillar);
            series.product_code = std::string(product_code);
            series.pillar = pillar;

            for (std::size_t i = 0; i < sessions.size(); ++i) {
                const SessionCurve& session = sessions[i];
                if (session.by_maturity.size() <= rank) {
                    continue;
                }
                const ContractQuote& occupant = session.by_maturity[rank];
                ++series.num_sessions_present;
                series.level_on_last_date = occupant.price;
                series.ticker_on_last_date = occupant.ticker;
                series.last_date = session.as_of;

                if (i == 0) {
                    continue;
                }
                // Strictly the previous session: never stretch a multi-session move
                // into something the caller would read as a daily return.
                const auto it = price_by_key.find(QuoteKey(occupant.ticker, sessions[i - 1].as_of));
                if (it == price_by_key.end() || it->second <= 0.0) {
                    continue;
                }
                series.returns.push_back(PillarReturn{
                        .as_of = session.as_of,
                        .log_return = std::log(occupant.price / it->second),
                });
            }

            if (!series.returns.empty()) {
                out.push_back(std::move(series));
            }
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadFuturesPillarReturns: "} + e.what());
    }
}

std::vector<FuturesPillarOccupant> LoadFuturesPillarCurve(const std::string& database_file_path,
                                                          const std::string_view product_code,
                                                          const std::string_view as_of_iso,
                                                          const int num_pillars) {
    if (num_pillars <= 0) {
        throw ValidationError("LoadFuturesPillarCurve: num_pillars must be > 0.");
    }

    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        return LoadOccupantsOnSession(db, product_code, as_of_iso, num_pillars);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadFuturesPillarCurve: "} + e.what());
    }
}

FuturesPillarCurve LoadLatestFuturesPillarCurve(const std::string& database_file_path,
                                                const std::string_view product_code,
                                                const std::string_view on_or_before_as_of_iso,
                                                const int num_pillars) {
    if (num_pillars <= 0) {
        throw ValidationError("LoadLatestFuturesPillarCurve: num_pillars must be > 0.");
    }

    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement session(db, kLatestOccupantSessionSql);
        session.bind(1, std::string(product_code));
        session.bind(2, std::string(on_or_before_as_of_iso));
        if (!session.executeStep() || session.getColumn(0).isNull()) {
            return {};
        }

        FuturesPillarCurve out{};
        out.as_of = session.getColumn(0).getString();
        out.occupants = LoadOccupantsOnSession(db, product_code, out.as_of, num_pillars);
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadLatestFuturesPillarCurve: "} + e.what());
    }
}

std::vector<FuturesCurveQuote> LoadFuturesCurveSnapshot(const std::string& database_file_path,
                                                        const std::string_view product_code,
                                                        const std::string_view as_of_iso) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(db, kOccupantSql);
        st.bind(1, std::string(product_code));
        st.bind(2, std::string(as_of_iso));

        std::vector<FuturesCurveQuote> out;
        while (st.executeStep()) {
            out.push_back(FuturesCurveQuote{
                    .ticker = st.getColumn(0).getString(),
                    .settlement_date = st.getColumn(1).getString(),
                    .price = st.getColumn(2).getDouble(),
            });
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadFuturesCurveSnapshot: "} + e.what());
    }
}

}  // namespace numeraire::database
