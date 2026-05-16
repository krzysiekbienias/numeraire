#include <SQLiteCpp/SQLiteCpp.h>
#include <cpr/util.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/market_data_providers/polygon_option_contract_fetch.hpp>
#include <numeraire/market_data_providers/polygon_ingest_common.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/logger.hpp>
#include <sstream>
#include <string>
#include <array>

#include <nlohmann/json.hpp>

namespace numeraire::market_data_providers {

using numeraire::database::BootstrapTradeDatabaseSchema;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;
using numeraire::market_data_providers::polygon_ingest::DataSourceLabelForBaseUrl;
using numeraire::market_data_providers::polygon_ingest::FetchJsonPage;
using numeraire::market_data_providers::polygon_ingest::IsoUtcNow;
using numeraire::market_data_providers::polygon_ingest::LooksIsoDate;
using numeraire::market_data_providers::polygon_ingest::PolygonApiKey;
using numeraire::market_data_providers::polygon_ingest::PolygonBaseUrl;
using numeraire::market_data_providers::polygon_ingest::SleepSecAfterPolygonCall;

namespace {

[[nodiscard]] bool IsoDateAddDays(const std::string& iso, const int delta_days, std::string& out) {
    int y = 0;
    int m = 0;
    int d = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
        return false;
    }
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    const time_t t0 = timegm(&tm);
    if (t0 == static_cast<time_t>(-1)) {
        return false;
    }
    const time_t t1 = t0 + (static_cast<time_t>(delta_days) * static_cast<time_t>(86400));
    gmtime_r(&t1, &tm);
    std::array<char, 16> buf{};
    strftime(buf.data(), buf.size(), "%Y-%m-%d", &tm);
    out.assign(buf.data());
    return true;
}

[[nodiscard]] int CompareIsoDate(const std::string& a, const std::string& b) {
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

[[nodiscard]] bool TryIndexClose(SQLite::Database& db,
                                 const std::string& index_ticker,
                                 const std::string& as_of,
                                 double& close_out) {
    SQLite::Statement q(db,
                        "SELECT close FROM index_daily_eod WHERE ticker = ? AND as_of = ? "
                        "AND timespan = '1d' AND adjusted = 1 LIMIT 1");
    q.bind(1, index_ticker);
    q.bind(2, as_of);
    if (!q.executeStep()) {
        return false;
    }
    close_out = q.getColumn(0).getDouble();
    return true;
}

[[nodiscard]] std::string BuildOptionsContractsUrl(const std::string& base,
                                                   const std::string& underlying,
                                                   const std::string& listing_as_of,
                                                   const std::string& contract_type,
                                                   const std::int64_t strike_min,
                                                   const std::int64_t strike_max) {
    std::ostringstream oss;
    oss << base << "/v3/reference/options/contracts?underlying_ticker=" << cpr::util::urlEncode(underlying)
        << "&as_of=" << cpr::util::urlEncode(listing_as_of) << "&contract_type=" << cpr::util::urlEncode(contract_type)
        << "&exercise_style=" << cpr::util::urlEncode(std::string("european"))
        << "&strike_price.gte=" << strike_min << "&strike_price.lte=" << strike_max
        << "&limit=1000&sort=strike_price";
    return oss.str();
}

[[nodiscard]] std::string NormalizeContractType(const std::string& raw) {
    if (raw == "call" || raw == "put") {
        return raw;
    }
    return "other";
}

[[nodiscard]] int UpsertContractRows(SQLite::Database& db,
                                     const nlohmann::json& results,
                                     const std::string& listing_as_of,
                                     const std::string& source_label,
                                     const std::string& ingested_at) {
    if (!results.is_array()) {
        return 0;
    }

    SQLite::Statement st(
            db,
            "INSERT INTO option_contract (option_ticker, listing_as_of, underlying_ticker, "
            "expiration_date, strike_price, contract_type, exercise_style, shares_per_contract, "
            "primary_exchange, cfi, currency, source, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(option_ticker, listing_as_of) DO UPDATE SET "
            "underlying_ticker=excluded.underlying_ticker, "
            "expiration_date=excluded.expiration_date, strike_price=excluded.strike_price, "
            "contract_type=excluded.contract_type, exercise_style=excluded.exercise_style, "
            "shares_per_contract=excluded.shares_per_contract, "
            "primary_exchange=excluded.primary_exchange, cfi=excluded.cfi, "
            "currency=excluded.currency, source=excluded.source, ingested_at=excluded.ingested_at");

    int n = 0;
    SQLite::Transaction txn(db);
    for (const auto& r : results) {
        if (!r.is_object()) {
            continue;
        }
        if (!r.contains("ticker") || !r["ticker"].is_string()) {
            continue;
        }
        const std::string option_ticker = r["ticker"].get<std::string>();
        const std::string underlying_ticker = r.value("underlying_ticker", "");
        const std::string expiration_date = r.value("expiration_date", "");
        if (underlying_ticker.empty() || expiration_date.empty()) {
            continue;
        }

        const std::string contract_type = NormalizeContractType(r.value("contract_type", "other"));
        const std::string exercise_style = r.value("exercise_style", "");

        int shares = 100;
        if (r.contains("shares_per_contract") && r["shares_per_contract"].is_number()) {
            const double sp = r["shares_per_contract"].get<double>();
            shares = static_cast<int>(std::lround(sp));
            if (shares <= 0) {
                shares = 100;
            }
        }

        std::string primary_ex;
        if (r.contains("primary_exchange")) {
            if (r["primary_exchange"].is_string()) {
                primary_ex = r["primary_exchange"].get<std::string>();
            }
        }

        std::string cfi;
        if (r.contains("cfi") && r["cfi"].is_string()) {
            cfi = r["cfi"].get<std::string>();
        }

        st.bind(1, option_ticker);
        st.bind(2, listing_as_of);
        st.bind(3, underlying_ticker);
        st.bind(4, expiration_date);
        st.bind(5, r.value("strike_price", 0.0));
        st.bind(6, contract_type);
        st.bind(7, exercise_style.empty() ? std::string("unknown") : exercise_style);
        st.bind(8, shares);
        if (primary_ex.empty()) {
            st.bind(9);
        } else {
            st.bind(9, primary_ex);
        }
        if (cfi.empty()) {
            st.bind(10);
        } else {
            st.bind(10, cfi);
        }
        st.bind(11, std::string("USD"));
        st.bind(12, source_label);
        st.bind(13, ingested_at);
        st.exec();
        st.reset();
        ++n;
    }
    txn.commit();
    return n;
}

[[nodiscard]] int IngestContractsForType(SQLite::Database& db,
                                           const std::string& base_url,
                                           const char* api_key,
                                           const std::string& underlying,
                                           const std::string& listing_as_of,
                                           const std::string& contract_type,
                                           const std::int64_t strike_min,
                                           const std::int64_t strike_max,
                                           const int throttle_sec,
                                           const std::string& source_label,
                                           const std::string& ingested_at) {
    std::string url = BuildOptionsContractsUrl(base_url, underlying, listing_as_of, contract_type, strike_min, strike_max);
    int total = 0;
    for (;;) {
        nlohmann::json j;
        std::string next_url;
        if (FetchJsonPage(url, api_key, throttle_sec, j, next_url) != 0) {
            return 1;
        }
        if (j.contains("results") && j["results"].is_array() && !j["results"].empty()) {
            const int chunk = UpsertContractRows(db, j["results"], listing_as_of, source_label, ingested_at);
            total += chunk;
            Logger::NumInfo("option contracts {} {} .. {} ({}): upserted {} row(s) (running {}).",
                            contract_type,
                            listing_as_of,
                            strike_min,
                            strike_max,
                            chunk,
                            total);
            if (j["results"].size() == 1000 && !next_url.empty()) {
                Logger::NumWarn("option contracts: page full (1000); following next_url (may exceed plan targets).");
            }
        } else {
            Logger::NumWarn("option contracts: no results for {} {} strike {}..{}.",
                            listing_as_of,
                            contract_type,
                            strike_min,
                            strike_max);
        }
        if (next_url.empty()) {
            break;
        }
        url = next_url;
    }
    return 0;
}

[[nodiscard]] int RunOneListingDay(SQLite::Database& db,
                                   const std::string& base_url,
                                   const char* api_key,
                                   const std::string& underlying,
                                   const std::string& index_ticker,
                                   const std::string& listing_as_of,
                                   const double strike_band,
                                   const int throttle_sec,
                                   const std::string& source_label) {
    double spot = 0.0;
    if (!TryIndexClose(db, index_ticker, listing_as_of, spot)) {
        Logger::NumError("No index_daily_eod row for ticker={} as_of={} (need close for strike band).",
                         index_ticker,
                         listing_as_of);
        return 1;
    }

    const auto strike_min = static_cast<std::int64_t>(std::llround(spot - strike_band));
    const auto strike_max = static_cast<std::int64_t>(std::llround(spot + strike_band));
    if (strike_min > strike_max) {
        Logger::NumError("Invalid strike band: spot={} band={} → min>max.", spot, strike_band);
        return 1;
    }

    const std::string ingested_at = IsoUtcNow();
    if (IngestContractsForType(
                db, base_url, api_key, underlying, listing_as_of, "call", strike_min, strike_max, throttle_sec, source_label, ingested_at) != 0) {
        return 1;
    }
    if (IngestContractsForType(
                db, base_url, api_key, underlying, listing_as_of, "put", strike_min, strike_max, throttle_sec, source_label, ingested_at) != 0) {
        return 1;
    }
    Logger::NumInfo("option contracts: finished {} (spot≈{} strikes {}..{}).",
                    listing_as_of,
                    spot,
                    strike_min,
                    strike_max);
    return 0;
}

}  // namespace

void PrintOptionContractFetchUsageLines() {
    Logger::NumError(
            "  dev_main --fetch-option-contracts --from YYYY-MM-DD --to YYYY-MM-DD --underlying NDX "
            "[--index-ticker I:NDX] [--strike-band 250]\n"
            "    Loads European options reference into `option_contract` (needs POLYGON_API_KEY).\n"
            "    Strike window: index close from `index_daily_eod` ± strike-band (default 250).\n"
            "    Default --index-ticker I:NDX when --underlying is NDX; else pass --index-ticker.\n"
            "    Base URL: POLYGON_BASE_URL. Throttle: NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL.");
}

int TryRunPolygonOptionContractFetch(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string from_iso;
    std::string to_iso;
    std::string underlying;
    std::string index_ticker;
    double strike_band = 250.0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fetch-option-contracts") == 0) {
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
        } else if (std::strcmp(argv[i], "--strike-band") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--strike-band requires a non-negative number (index points).");
                return 1;
            }
            char* end = nullptr;
            const double v = std::strtod(argv[++i], &end);
            if (end == argv[i] || v < 0.0) {
                Logger::NumError("--strike-band must be a non-negative number.");
                return 1;
            }
            strike_band = v;
        }
    }

    if (!mode) {
        return -1;
    }

    if (from_iso.empty() || to_iso.empty()) {
        Logger::NumError("--fetch-option-contracts requires --from and --to.");
        PrintOptionContractFetchUsageLines();
        return 1;
    }
    if (!LooksIsoDate(from_iso) || !LooksIsoDate(to_iso)) {
        Logger::NumError("--from/--to must be YYYY-MM-DD.");
        return 1;
    }
    if (CompareIsoDate(from_iso, to_iso) > 0) {
        Logger::NumError("--from must be on or before --to.");
        return 1;
    }
    if (underlying.empty()) {
        Logger::NumError("--fetch-option-contracts requires --underlying.");
        PrintOptionContractFetchUsageLines();
        return 1;
    }
    if (index_ticker.empty()) {
        if (underlying == "NDX") {
            index_ticker = "I:NDX";
        } else {
            Logger::NumError("--index-ticker is required unless --underlying is NDX (default I:NDX).");
            PrintOptionContractFetchUsageLines();
            return 1;
        }
    }

    const char* key = PolygonApiKey();
    if (key == nullptr || key[0] == '\0') {
        Logger::NumError("POLYGON_API_KEY is not set (e.g. in `.env`).");
        return 1;
    }

    const std::string base = PolygonBaseUrl();
    const int throttle_sec = SleepSecAfterPolygonCall();
    const std::string source_label = DataSourceLabelForBaseUrl(base);
    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");
    Logger::NumInfo("option_contract ingest → SQLite {}", db_path.string());

    try {
        SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("PRAGMA foreign_keys = ON;");

        std::string day = from_iso;
        for (;;) {
            if (RunOneListingDay(db, base, key, underlying, index_ticker, day, strike_band, throttle_sec, source_label) != 0) {
                return 1;
            }
            if (day == to_iso) {
                break;
            }
            std::string next_day;
            if (!IsoDateAddDays(day, 1, next_day)) {
                Logger::NumError("Internal date advance failed after {}.", day);
                return 1;
            }
            day = std::move(next_day);
            if (CompareIsoDate(day, to_iso) > 0) {
                break;
            }
        }
    } catch (const SQLite::Exception& e) {
        Logger::NumError("SQLite: {}", e.what());
        return 1;
    }

    Logger::NumInfo("--fetch-option-contracts finished.");
    return 0;
}

}  // namespace numeraire::market_data_providers
