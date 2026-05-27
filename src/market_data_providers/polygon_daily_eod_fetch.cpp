#include <SQLiteCpp/SQLiteCpp.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/market_data_providers/polygon_daily_eod_fetch.hpp>
#include <numeraire/market_data_providers/polygon_ingest_common.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/logger.hpp>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace numeraire::market_data_providers {

using numeraire::database::BootstrapTradeDatabaseSchema;
using numeraire::market_data_providers::polygon_ingest::AsOfIsoFromPolygonBarMs;
using numeraire::market_data_providers::polygon_ingest::DataSourceLabelForBaseUrl;
using numeraire::market_data_providers::polygon_ingest::FetchJsonPage;
using numeraire::market_data_providers::polygon_ingest::IsoUtcNow;
using numeraire::market_data_providers::polygon_ingest::LooksIsoDate;
using numeraire::market_data_providers::polygon_ingest::PolygonApiKey;
using numeraire::market_data_providers::polygon_ingest::PolygonBaseUrl;
using numeraire::market_data_providers::polygon_ingest::SleepSecAfterPolygonEquityCall;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

namespace {

[[nodiscard]] std::string BuildAggsUrl(const std::string& base,
                                       const std::string& ticker,
                                       const std::string& from_iso,
                                       const std::string& to_iso,
                                       const bool adjusted) {
    std::ostringstream oss;
    oss << base << "/v2/aggs/ticker/" << ticker << "/range/1/day/" << from_iso << '/' << to_iso
        << "?adjusted=" << (adjusted ? "true" : "false") << "&sort=asc&limit=50000";
    return oss.str();
}

[[nodiscard]] int UpsertBars(SQLite::Database& db,
                             const std::string& ticker,
                             const nlohmann::json& results,
                             const bool adjusted_flag,
                             const std::string& source_label,
                             const std::string& ingested_at) {
    if (!results.is_array()) {
        return 0;
    }

    SQLite::Statement st(db,
                         "INSERT INTO equity_daily_eod ("
                         "ticker, as_of, session_calendar, open, high, low, close, currency, "
                         "volume, vwap, trade_count, source, timespan, adjusted, "
                         "provider_timestamp_utc_ms, ingested_at) "
                         "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                         "ON CONFLICT(ticker, as_of, timespan, adjusted) DO UPDATE SET "
                         "session_calendar=excluded.session_calendar, "
                         "open=excluded.open, high=excluded.high, low=excluded.low, close=excluded.close, "
                         "currency=excluded.currency, volume=excluded.volume, vwap=excluded.vwap, "
                         "trade_count=excluded.trade_count, source=excluded.source, "
                         "provider_timestamp_utc_ms=excluded.provider_timestamp_utc_ms, "
                         "ingested_at=excluded.ingested_at");

    int n_rows = 0;
    SQLite::Transaction txn(db);
    for (const auto& r : results) {
        if (!r.is_object()) {
            continue;
        }
        const std::int64_t t_ms = r.value("t", 0LL);
        if (t_ms <= 0) {
            continue;
        }
        const std::string as_of = AsOfIsoFromPolygonBarMs(t_ms);

        st.bind(1, ticker);
        st.bind(2, as_of);
        st.bind(3, std::string("America/New_York"));
        st.bind(4, r.value("o", 0.0));
        st.bind(5, r.value("h", 0.0));
        st.bind(6, r.value("l", 0.0));
        st.bind(7, r.value("c", 0.0));
        st.bind(8, std::string("USD"));

        if (r.contains("v") && r["v"].is_number()) {
            st.bind(9, r["v"].get<double>());
        } else {
            st.bind(9);
        }
        if (r.contains("vw") && r["vw"].is_number()) {
            st.bind(10, r["vw"].get<double>());
        } else {
            st.bind(10);
        }
        if (r.contains("n") && r["n"].is_number_integer()) {
            st.bind(11, r["n"].get<std::int64_t>());
        } else {
            st.bind(11);
        }
        st.bind(12, source_label);
        st.bind(13, std::string("1d"));
        st.bind(14, adjusted_flag ? 1 : 0);
        st.bind(15, t_ms);
        st.bind(16, ingested_at);

        st.exec();
        st.reset();

        ++n_rows;
    }
    txn.commit();
    return n_rows;
}

[[nodiscard]] int IngestTickerRange(SQLite::Database& db,
                                    const std::string& base_url,
                                    const char* api_key,
                                    const std::string& ticker,
                                    const std::string& from_iso,
                                    const std::string& to_iso,
                                    const bool adjusted,
                                    const int throttle_sec) {
    const std::string source_label = DataSourceLabelForBaseUrl(base_url);
    std::string url = BuildAggsUrl(base_url, ticker, from_iso, to_iso, adjusted);
    int total_upserted = 0;
    const std::string ingested_at = IsoUtcNow();

    for (;;) {
        nlohmann::json j;
        std::string next_url;
        if (FetchJsonPage(url, api_key, throttle_sec, j, next_url) != 0) {
            return 1;
        }
        if (!j.contains("results") || !j["results"].is_array() || j["results"].empty()) {
            Logger::NumWarn("Polygon: no daily results for {} in range {}..{}.", ticker, from_iso, to_iso);
            break;
        }
        const int chunk = UpsertBars(db, ticker, j["results"], adjusted, source_label, ingested_at);
        total_upserted += chunk;
        Logger::NumInfo("Polygon: upserted {} row(s) for {} (chunk running total {}).", chunk, ticker, total_upserted);
        if (next_url.empty()) {
            break;
        }
        url = next_url;
    }

    Logger::NumInfo("Polygon: done {} total bar row(s) for {}.", total_upserted, ticker);
    return 0;
}

}  // namespace

void PrintFetchUsageLines() {
    Logger::NumError(
            "  dev_main --fetch-eod-daily --from YYYY-MM-DD --to YYYY-MM-DD --ticker SYM "
            "[--ticker SYM2 ...] [--raw]\n"
            "    Upsert Polygon v2/aggs 1/day into `equity_daily_eod` (needs POLYGON_API_KEY).\n"
            "    Default adjusted=true; pass --raw for adjusted=false.\n"
            "    Base URL: POLYGON_BASE_URL (default https://api.polygon.io; e.g. https://api.massive.com).\n"
            "    Throttle: NUMERAIRE_POLYGON_EQUITY_PLAN=basic|starter or NUMERAIRE_POLYGON_EQUITY_SLEEP_SEC "
            "(legacy: NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL; default 13).");
}

int TryRunPolygonDailyEodFetch(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool fetch_mode = false;
    std::string from_iso;
    std::string to_iso;
    std::vector<std::string> tickers;
    bool adjusted = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fetch-eod-daily") == 0) {
            fetch_mode = true;
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
        } else if (std::strcmp(argv[i], "--ticker") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--ticker requires a symbol.");
                return 1;
            }
            tickers.emplace_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--raw") == 0) {
            adjusted = false;
        }
    }

    if (!fetch_mode) {
        return -1;
    }

    if (from_iso.empty() || to_iso.empty()) {
        Logger::NumError("--fetch-eod-daily requires --from and --to.");
        PrintFetchUsageLines();
        return 1;
    }
    if (!LooksIsoDate(from_iso) || !LooksIsoDate(to_iso)) {
        Logger::NumError("--from/--to must be YYYY-MM-DD.");
        return 1;
    }
    if (tickers.empty()) {
        Logger::NumError("--fetch-eod-daily requires at least one --ticker.");
        PrintFetchUsageLines();
        return 1;
    }

    const char* key = PolygonApiKey();
    if (key == nullptr || key[0] == '\0') {
        Logger::NumError("POLYGON_API_KEY is not set (e.g. in `.env`).");
        return 1;
    }

    const std::string base = PolygonBaseUrl();
    const int throttle_sec = SleepSecAfterPolygonEquityCall();
    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");
    Logger::NumInfo("equity_daily_eod ingest → SQLite {}", db_path.string());

    try {
        SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("PRAGMA foreign_keys = ON;");

        for (const std::string& t : tickers) {
            if (IngestTickerRange(db, base, key, t, from_iso, to_iso, adjusted, throttle_sec) != 0) {
                return 1;
            }
        }
    } catch (const SQLite::Exception& e) {
        Logger::NumError("SQLite: {}", e.what());
        return 1;
    }

    Logger::NumInfo("fetch-eod-daily finished for {} ticker(s).", tickers.size());
    return 0;
}

}  // namespace numeraire::market_data_providers
