#include <numeraire/database/option_universe_eod_builder.hpp>

#include <numeraire/database/index_daily_eod_lookup.hpp>
#include <numeraire/database/option_universe_grid_config.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace numeraire::database {
namespace {

using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;
using numeraire::ConfigError;
using numeraire::ValidationError;
using schedule::Date;
using schedule::FromQuantLibDate;
using schedule::ParseIsoDate;
using schedule::ToQuantLibDate;

[[nodiscard]] std::string IsoUtcNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&t, &utc);
    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

[[nodiscard]] bool LooksIsoDate(const std::string& s) {
    return s.size() == 10U && s[4] == '-' && s[7] == '-';
}

[[nodiscard]] int CompareIsoDate(const std::string& a, const std::string& b) {
    if (a == b) {
        return 0;
    }
    return a < b ? -1 : 1;
}

[[nodiscard]] std::string FormatIsoDate(const Date& d) {
    std::ostringstream oss;
    oss << std::setfill('0') << d.year << '-' << std::setw(2) << d.month << '-' << std::setw(2) << d.day;
    return oss.str();
}

[[nodiscard]] int CalendarDteDays(const std::string& listing_as_of, const std::string& expiration_date) {
    const Date listing = ParseIsoDate(listing_as_of);
    const Date expiry = ParseIsoDate(expiration_date);
    const auto ql_delta = ToQuantLibDate(expiry) - ToQuantLibDate(listing);
    return static_cast<int>(ql_delta);
}

[[nodiscard]] double MinAdjacentOtmStepPercent(const std::vector<double>& percents) {
    if (percents.size() < 2U) {
        return percents.empty() ? 1.0 : percents.front();
    }
    double min_step = std::numeric_limits<double>::max();
    for (size_t i = 1; i < percents.size(); ++i) {
        min_step = std::min(min_step, percents[i] - percents[i - 1]);
    }
    return min_step > 0.0 ? min_step : 1.0;
}

[[nodiscard]] double MaxStrikeGapPctOfSpot(const double spot,
                                           const std::vector<double>& otm_percents,
                                           const double skip_ratio) {
    const double half_step = MinAdjacentOtmStepPercent(otm_percents) * 0.5;
    return skip_ratio * half_step;
}

struct CatalogRow {
    std::string option_ticker;
    std::string expiration_date;
    double strike_price{0.0};
    std::string contract_type;
};

[[nodiscard]] double TargetStrike(const double spot, const double otm_percent, const std::string& contract_type) {
    if (contract_type == "call") {
        return spot * (1.0 + otm_percent / 100.0);
    }
    return spot * (1.0 - otm_percent / 100.0);
}

[[nodiscard]] std::optional<CatalogRow> FindNearestStrike(
        const std::vector<CatalogRow>& rows, const double target_strike) {
    if (rows.empty()) {
        return std::nullopt;
    }
    const CatalogRow* best = &rows.front();
    double best_gap = std::abs(rows.front().strike_price - target_strike);
    for (size_t i = 1; i < rows.size(); ++i) {
        const double gap = std::abs(rows[i].strike_price - target_strike);
        if (gap < best_gap) {
            best_gap = gap;
            best = &rows[i];
        }
    }
    return *best;
}

[[nodiscard]] std::vector<CatalogRow> LoadCatalog(SQLite::Database& db,
                                                  const std::string& listing_as_of,
                                                  const std::string& underlying) {
    SQLite::Statement st(
            db,
            "SELECT option_ticker, expiration_date, strike_price, contract_type "
            "FROM option_contract WHERE listing_as_of = ? AND underlying_ticker = ?");
    st.bind(1, listing_as_of);
    st.bind(2, underlying);

    std::vector<CatalogRow> rows;
    while (st.executeStep()) {
        CatalogRow row;
        row.option_ticker = st.getColumn(0).getString();
        row.expiration_date = st.getColumn(1).getString();
        row.strike_price = st.getColumn(2).getDouble();
        row.contract_type = st.getColumn(3).getString();
        rows.push_back(std::move(row));
    }
    return rows;
}

[[nodiscard]] std::string PickNearestExpiry(const std::map<std::string, int>& expiry_to_dte, const int target_dte) {
    std::string best_exp;
    int best_gap = std::numeric_limits<int>::max();
    for (const auto& [exp, dte] : expiry_to_dte) {
        const int gap = std::abs(dte - target_dte);
        if (gap < best_gap || (gap == best_gap && (best_exp.empty() || exp < best_exp))) {
            best_gap = gap;
            best_exp = exp;
        }
    }
    return best_exp;
}

void DeleteExistingUniverse(SQLite::Database& db,
                            const std::string& listing_as_of,
                            const std::string& underlying,
                            const std::string& grid_name) {
    SQLite::Statement del(
            db,
            "DELETE FROM option_universe_eod WHERE listing_as_of = ? AND underlying_ticker = ? AND "
            "grid_config_name = ?");
    del.bind(1, listing_as_of);
    del.bind(2, underlying);
    del.bind(3, grid_name);
    del.exec();
}

}  // namespace

OptionUniverseBuildStats BuildOptionUniverseEod(const OptionUniverseBuildParams& params,
                                                const OptionUniverseGridConfig& grid) {
    const auto spot_opt = LookupIndexDailyClose(
            params.database_file_path, params.index_ticker, params.listing_as_of, params.spot_adjusted);
    if (!spot_opt || *spot_opt <= 0.0) {
        throw ValidationError("No index_daily_eod close for ticker=" + params.index_ticker + " as_of=" +
                              params.listing_as_of);
    }
    const double spot = *spot_opt;

    SQLite::Database db(params.database_file_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON;");

    const std::vector<CatalogRow> catalog = LoadCatalog(db, params.listing_as_of, params.underlying_ticker);
    if (catalog.empty()) {
        throw ValidationError("No option_contract rows for listing_as_of=" + params.listing_as_of + " underlying=" +
                              params.underlying_ticker);
    }

    std::map<std::string, int> expiry_to_dte;
    std::map<std::string, std::map<std::string, std::vector<CatalogRow>>> by_expiry_type;
    for (const CatalogRow& row : catalog) {
        if (expiry_to_dte.find(row.expiration_date) == expiry_to_dte.end()) {
            expiry_to_dte[row.expiration_date] = CalendarDteDays(params.listing_as_of, row.expiration_date);
        }
        by_expiry_type[row.expiration_date][row.contract_type].push_back(row);
    }

    const double max_gap_pct = MaxStrikeGapPctOfSpot(spot, grid.otm_moneyness_percent,
                                                   grid.skip_point_if_strike_gap_exceeds_ratio);
    const std::string built_at = IsoUtcNow();

    DeleteExistingUniverse(db, params.listing_as_of, params.underlying_ticker, grid.name);

    SQLite::Statement ins(
            db,
            "INSERT INTO option_universe_eod ("
            "listing_as_of, underlying_ticker, grid_config_name, option_ticker, pillar_id, target_dte_days, "
            "expiration_date, otm_percent, contract_type, strike_price, target_strike, spot_used, strike_gap_pct, "
            "built_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)");

    OptionUniverseBuildStats stats;
    std::set<std::string> seen_tickers;

    SQLite::Transaction txn(db);
    for (const ExpiryPillarConfig& pillar : grid.expiry_pillars) {
        const std::string expiry = PickNearestExpiry(expiry_to_dte, pillar.target_dte_days);
        if (expiry.empty()) {
            continue;
        }

        for (const double otm_percent : grid.otm_moneyness_percent) {
            const std::vector<std::string> contract_types =
                    (otm_percent == 0.0) ? std::vector<std::string>{"call", "put"} : std::vector<std::string>{"call", "put"};

            for (const std::string& contract_type : contract_types) {
                ++stats.points_requested;

                const auto type_it = by_expiry_type.find(expiry);
                if (type_it == by_expiry_type.end()) {
                    ++stats.points_skipped_no_contract;
                    continue;
                }
                const auto rows_it = type_it->second.find(contract_type);
                if (rows_it == type_it->second.end() || rows_it->second.empty()) {
                    ++stats.points_skipped_no_contract;
                    continue;
                }

                const double target_k = TargetStrike(spot, otm_percent, contract_type);
                const std::optional<CatalogRow> match = FindNearestStrike(rows_it->second, target_k);
                if (!match) {
                    ++stats.points_skipped_no_contract;
                    continue;
                }

                const double gap_pct = 100.0 * std::abs(match->strike_price - target_k) / spot;
                if (gap_pct > max_gap_pct) {
                    ++stats.points_skipped_strike_gap;
                    continue;
                }

                if (seen_tickers.count(match->option_ticker) != 0) {
                    continue;
                }
                seen_tickers.insert(match->option_ticker);

                ins.bind(1, params.listing_as_of);
                ins.bind(2, params.underlying_ticker);
                ins.bind(3, grid.name);
                ins.bind(4, match->option_ticker);
                ins.bind(5, pillar.id);
                ins.bind(6, pillar.target_dte_days);
                ins.bind(7, expiry);
                ins.bind(8, otm_percent);
                ins.bind(9, contract_type);
                ins.bind(10, match->strike_price);
                ins.bind(11, target_k);
                ins.bind(12, spot);
                ins.bind(13, gap_pct);
                ins.bind(14, built_at);
                ins.exec();
                ins.reset();

                ++stats.points_matched;
                ++stats.rows_upserted;
            }
        }
    }

    txn.commit();
    return stats;
}

void PrintOptionUniverseEodBuildUsageLines() {
    Logger::NumError(
            "  dev_main --build-option-universe --from YYYY-MM-DD --to YYYY-MM-DD --underlying NDX "
            "[--index-ticker I:NDX] [--grid-config configs/option_universe_grid.json]\n"
            "    Map `option_contract` → `option_universe_eod` using parametric grid JSON (nearest expiry/strike).\n"
            "    Requires index_daily_eod close on each listing_as_of. Run before --fetch-option-daily-price-eod.");
}

int TryRunOptionUniverseEodBuild(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string from_iso;
    std::string to_iso;
    std::string underlying;
    std::string index_ticker;
    std::filesystem::path grid_path = "configs/option_universe_grid.json";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--build-option-universe") == 0) {
            mode = true;
        } else if (std::strcmp(argv[i], "--from") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--from requires YYYY-MM-DD.");
                return 1;
            }
            from_iso = argv[++i];
        } else if (std::strcmp(argv[i], "--to") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--to requires YYYY-MM-DD.");
                return 1;
            }
            to_iso = argv[++i];
        } else if (std::strcmp(argv[i], "--underlying") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--underlying requires a symbol.");
                return 1;
            }
            underlying = argv[++i];
        } else if (std::strcmp(argv[i], "--index-ticker") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--index-ticker requires a symbol.");
                return 1;
            }
            index_ticker = argv[++i];
        } else if (std::strcmp(argv[i], "--grid-config") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--grid-config requires a path.");
                return 1;
            }
            grid_path = argv[++i];
        }
    }

    if (!mode) {
        return -1;
    }

    if (from_iso.empty() || to_iso.empty() || underlying.empty()) {
        Logger::NumError("--build-option-universe requires --from, --to, and --underlying.");
        PrintOptionUniverseEodBuildUsageLines();
        return 1;
    }
    if (!LooksIsoDate(from_iso) || !LooksIsoDate(to_iso)) {
        Logger::NumError("--from/--to must be YYYY-MM-DD.");
        return 1;
    }
    if (CompareIsoDate(from_iso, to_iso) > 0) {
        Logger::NumError("--from must be <= --to.");
        return 1;
    }

    if (index_ticker.empty()) {
        if (underlying == "NDX") {
            index_ticker = "I:NDX";
        } else {
            Logger::NumError("--index-ticker is required unless --underlying is NDX (default I:NDX).");
            return 1;
        }
    }

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    OptionUniverseGridConfig grid;
    try {
        grid = LoadOptionUniverseGridConfig(grid_path);
    } catch (const ConfigError& ex) {
        Logger::NumError("{}", ex.what());
        return 1;
    }

    const int adjusted =
            (std::getenv("NUMERAIRE_DEV_SPOT_ADJUSTED") != nullptr &&
             std::strcmp(std::getenv("NUMERAIRE_DEV_SPOT_ADJUSTED"), "0") == 0)
                    ? 0
                    : 1;

    Logger::NumInfo("build-option-universe → SQLite {} underlying={} index={} grid={} range {}..{}.",
                    db_path.string(),
                    underlying,
                    index_ticker,
                    grid.name,
                    from_iso,
                    to_iso);

    Date cursor = ParseIsoDate(from_iso);
    const Date end = ParseIsoDate(to_iso);
    int days_ok = 0;
    for (;;) {
        OptionUniverseBuildParams params{};
        params.database_file_path = db_path.string();
        params.listing_as_of = FormatIsoDate(cursor);
        params.underlying_ticker = underlying;
        params.index_ticker = index_ticker;
        params.grid_config_path = grid_path;
        params.spot_adjusted = adjusted;

        try {
            const OptionUniverseBuildStats stats = BuildOptionUniverseEod(params, grid);
            Logger::NumInfo(
                    "option universe {}: matched={} skipped_no_contract={} skipped_gap={} rows={}.",
                    params.listing_as_of,
                    stats.points_matched,
                    stats.points_skipped_no_contract,
                    stats.points_skipped_strike_gap,
                    stats.rows_upserted);
            ++days_ok;
        } catch (const ValidationError& ex) {
            Logger::NumWarn("Skipping {}: {}.", params.listing_as_of, ex.what());
        }

        if (cursor.year == end.year && cursor.month == end.month && cursor.day == end.day) {
            break;
        }
        cursor = FromQuantLibDate(ToQuantLibDate(cursor) + 1);
    }

    Logger::NumInfo("build-option-universe finished: {} day(s) in range {}..{}.", days_ok, from_iso, to_iso);
    return 0;
}

}  // namespace numeraire::database
