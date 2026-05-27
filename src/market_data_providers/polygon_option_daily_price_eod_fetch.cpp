#include <SQLiteCpp/SQLiteCpp.h>
#include <cpr/util.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/market_data_providers/polygon_option_daily_price_eod_fetch.hpp>
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
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;
using numeraire::market_data_providers::polygon_ingest::AsOfIsoFromPolygonBarMs;
using numeraire::market_data_providers::polygon_ingest::DataSourceLabelForBaseUrl;
using numeraire::market_data_providers::polygon_ingest::FetchJsonPage;
using numeraire::market_data_providers::polygon_ingest::IsoUtcNow;
using numeraire::market_data_providers::polygon_ingest::LooksIsoDate;
using numeraire::market_data_providers::polygon_ingest::PolygonApiKey;
using numeraire::market_data_providers::polygon_ingest::PolygonBaseUrl;
using numeraire::market_data_providers::polygon_ingest::SleepSecAfterPolygonOptionsCall;

namespace {

[[nodiscard]] std::string BuildOptionAggsUrl(const std::string& base,
                                             const std::string& option_ticker,
                                             const std::string& from_iso,
                                             const std::string& to_iso,
                                             const bool adjusted) {
    std::ostringstream oss;
    oss << base << "/v2/aggs/ticker/" << cpr::util::urlEncode(option_ticker) << "/range/1/day/" << from_iso
        << '/' << to_iso << "?adjusted=" << (adjusted ? "true" : "false") << "&sort=asc&limit=50000";
    return oss.str();
}

[[nodiscard]] int UpsertOptionBars(SQLite::Database& db,
                                   const std::string& option_ticker,
                                   const nlohmann::json& results,
                                   const bool adjusted_flag,
                                   const std::string& source_label,
                                   const std::string& ingested_at) {
    if (!results.is_array()) {
        return 0;
    }

    SQLite::Statement st(
            db,
            "INSERT INTO option_daily_price_eod ("
            "option_ticker, as_of, session_calendar, open, high, low, close, currency, "
            "volume, vwap, trade_count, source, timespan, adjusted, "
            "provider_timestamp_utc_ms, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(option_ticker, as_of, timespan, adjusted) DO UPDATE SET "
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

        st.bind(1, option_ticker);
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

[[nodiscard]] int IngestOptionTickerRange(SQLite::Database& db,
                                          const std::string& base_url,
                                          const char* api_key,
                                          const std::string& option_ticker,
                                          const std::string& from_iso,
                                          const std::string& to_iso,
                                          const bool adjusted,
                                          const int throttle_sec) {
    const std::string source_label = DataSourceLabelForBaseUrl(base_url);
    std::string url = BuildOptionAggsUrl(base_url, option_ticker, from_iso, to_iso, adjusted);
    int total_upserted = 0;
    const std::string ingested_at = IsoUtcNow();

    for (;;) {
        nlohmann::json j;
        std::string next_url;
        if (FetchJsonPage(url, api_key, throttle_sec, j, next_url) != 0) {
            return 1;
        }
        if (!j.contains("results") || !j["results"].is_array() || j["results"].empty()) {
            Logger::NumWarn("Polygon: no daily option bars for {} in range {}..{}.", option_ticker, from_iso, to_iso);
            break;
        }
        const int chunk = UpsertOptionBars(db, option_ticker, j["results"], adjusted, source_label, ingested_at);
        total_upserted += chunk;
        Logger::NumInfo("Polygon option EOD: upserted {} row(s) for {} (running total {}).",
                        chunk,
                        option_ticker,
                        total_upserted);
        if (next_url.empty()) {
            break;
        }
        url = next_url;
    }

    Logger::NumInfo("Polygon option EOD: done {} bar row(s) for {}.", total_upserted, option_ticker);
    return 0;
}

[[nodiscard]] std::vector<std::string> LoadOptionTickersFromUniverse(SQLite::Database& db,
                                                                     const std::string& listing_as_of,
                                                                     const std::string& underlying,
                                                                     const std::string& grid_config_name) {
    SQLite::Statement q(
            db,
            "SELECT option_ticker FROM option_universe_eod WHERE listing_as_of = ? AND underlying_ticker = ? AND "
            "grid_config_name = ? ORDER BY expiration_date, strike_price, option_ticker");
    q.bind(1, listing_as_of);
    q.bind(2, underlying);
    q.bind(3, grid_config_name);

    std::vector<std::string> tickers;
    while (q.executeStep()) {
        tickers.emplace_back(q.getColumn(0).getString());
    }
    return tickers;
}

[[nodiscard]] std::vector<std::string> LoadOptionTickersFromCatalog(SQLite::Database& db,
                                                                    const std::string& listing_as_of,
                                                                    const std::string& underlying,
                                                                    const std::string& expiration_date,
                                                                    const int limit) {
    std::ostringstream sql;
    sql << "SELECT option_ticker FROM option_contract WHERE listing_as_of = ? AND underlying_ticker = ?";
    if (!expiration_date.empty()) {
        sql << " AND expiration_date = ?";
    }
    sql << " ORDER BY expiration_date, strike_price, option_ticker";
    if (limit > 0) {
        sql << " LIMIT ?";
    }

    SQLite::Statement q(db, sql.str());
    int bind_idx = 1;
    q.bind(bind_idx++, listing_as_of);
    q.bind(bind_idx++, underlying);
    if (!expiration_date.empty()) {
        q.bind(bind_idx++, expiration_date);
    }
    if (limit > 0) {
        q.bind(bind_idx++, limit);
    }

    std::vector<std::string> tickers;
    while (q.executeStep()) {
        tickers.emplace_back(q.getColumn(0).getString());
    }
    return tickers;
}

}  // namespace

void PrintOptionDailyPriceEodFetchUsageLines() {
    Logger::NumError(
            "  dev_main --fetch-option-daily-price-eod --from YYYY-MM-DD --to YYYY-MM-DD "
            "[--listing-as-of YYYY-MM-DD --underlying NDX] [--grid-config NAME] [--from-catalog] "
            "[--option-ticker O:… …] [--expiration-date YYYY-MM-DD] [--limit N] [--raw]\n"
            "    Upsert Polygon v2/aggs 1/day into `option_daily_price_eod` (needs POLYGON_API_KEY).\n"
            "    Tickers: --option-ticker, else `option_universe_eod` (run --build-option-universe first), "
            "or --from-catalog for full `option_contract`.\n"
            "    Throttle: NUMERAIRE_POLYGON_OPTIONS_PLAN=starter|basic or NUMERAIRE_POLYGON_OPTIONS_SLEEP_SEC.");
}

int TryRunPolygonOptionDailyPriceEodFetch(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string from_iso;
    std::string to_iso;
    std::string listing_as_of;
    std::string underlying;
    std::string expiration_date;
    std::string grid_config_name = "default_index_option_universe";
    std::vector<std::string> explicit_tickers;
    int limit = 0;
    bool adjusted = true;
    bool from_catalog = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fetch-option-daily-price-eod") == 0) {
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
        } else if (std::strcmp(argv[i], "--listing-as-of") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--listing-as-of requires YYYY-MM-DD.");
                return 1;
            }
            listing_as_of = argv[++i];
        } else if (std::strcmp(argv[i], "--underlying") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--underlying requires a symbol.");
                return 1;
            }
            underlying = argv[++i];
        } else if (std::strcmp(argv[i], "--option-ticker") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--option-ticker requires an options symbol (e.g. O:NDXP260501C27410000).");
                return 1;
            }
            explicit_tickers.emplace_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--expiration-date") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--expiration-date requires YYYY-MM-DD.");
                return 1;
            }
            expiration_date = argv[++i];
        } else if (std::strcmp(argv[i], "--limit") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--limit requires a positive integer.");
                return 1;
            }
            char* end = nullptr;
            const long v = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || v <= 0) {
                Logger::NumError("--limit must be a positive integer.");
                return 1;
            }
            limit = static_cast<int>(v);
        } else if (std::strcmp(argv[i], "--grid-config") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--grid-config requires the grid name from option_universe_grid.json (name field).");
                return 1;
            }
            grid_config_name = argv[++i];
        } else if (std::strcmp(argv[i], "--from-catalog") == 0) {
            from_catalog = true;
        } else if (std::strcmp(argv[i], "--raw") == 0) {
            adjusted = false;
        }
    }

    if (!mode) {
        return -1;
    }

    if (from_iso.empty() || to_iso.empty()) {
        Logger::NumError("--fetch-option-daily-price-eod requires --from and --to.");
        PrintOptionDailyPriceEodFetchUsageLines();
        return 1;
    }
    if (!LooksIsoDate(from_iso) || !LooksIsoDate(to_iso)) {
        Logger::NumError("--from/--to must be YYYY-MM-DD.");
        return 1;
    }
    if (!listing_as_of.empty() && !LooksIsoDate(listing_as_of)) {
        Logger::NumError("--listing-as-of must be YYYY-MM-DD.");
        return 1;
    }
    if (!expiration_date.empty() && !LooksIsoDate(expiration_date)) {
        Logger::NumError("--expiration-date must be YYYY-MM-DD.");
        return 1;
    }

    const char* key = PolygonApiKey();
    if (key == nullptr || key[0] == '\0') {
        Logger::NumError("POLYGON_API_KEY is not set (e.g. in `.env`).");
        return 1;
    }

    const std::string base = PolygonBaseUrl();
    const int throttle_sec = SleepSecAfterPolygonOptionsCall();
    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");
    Logger::NumInfo("option_daily_price_eod ingest → SQLite {} (throttle {}s after each call).",
                    db_path.string(),
                    throttle_sec);

    try {
        SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("PRAGMA foreign_keys = ON;");

        std::vector<std::string> tickers = explicit_tickers;
        if (tickers.empty()) {
            if (listing_as_of.empty() || underlying.empty()) {
                Logger::NumError(
                        "Provide --option-ticker … and/or both --listing-as-of and --underlying for catalog lookup.");
                PrintOptionDailyPriceEodFetchUsageLines();
                return 1;
            }
            if (from_catalog) {
                tickers = LoadOptionTickersFromCatalog(db, listing_as_of, underlying, expiration_date, limit);
            } else {
                tickers = LoadOptionTickersFromUniverse(db, listing_as_of, underlying, grid_config_name);
                if (tickers.empty()) {
                    Logger::NumError(
                            "No rows in option_universe_eod for listing={} underlying={} grid={}. "
                            "Run --build-option-universe first or pass --from-catalog.",
                            listing_as_of,
                            underlying,
                            grid_config_name);
                    return 1;
                }
                if (limit > 0 && static_cast<int>(tickers.size()) > limit) {
                    tickers.resize(static_cast<size_t>(limit));
                }
            }
        } else if (limit > 0 && static_cast<int>(tickers.size()) > limit) {
            tickers.resize(static_cast<size_t>(limit));
        }

        if (tickers.empty()) {
            Logger::NumError("No option tickers to fetch (empty catalog slice or no --option-ticker).");
            return 1;
        }

        Logger::NumInfo("Fetching option EOD bars for {} ticker(s), range {}..{}.", tickers.size(), from_iso, to_iso);

        int failed = 0;
        int total_bars = 0;
        for (const std::string& t : tickers) {
            if (IngestOptionTickerRange(db, base, key, t, from_iso, to_iso, adjusted, throttle_sec) != 0) {
                ++failed;
                continue;
            }
            SQLite::Statement cnt(
                    db,
                    "SELECT COUNT(*) FROM option_daily_price_eod WHERE option_ticker = ? AND as_of >= ? AND as_of <= ?");
            cnt.bind(1, t);
            cnt.bind(2, from_iso);
            cnt.bind(3, to_iso);
            if (cnt.executeStep()) {
                total_bars += cnt.getColumn(0).getInt();
            }
        }

        Logger::NumInfo("fetch-option-daily-price-eod finished: {} ticker(s), {} failed, ~{} bar row(s) in range.",
                        tickers.size(),
                        failed,
                        total_bars);
        return failed > 0 ? 1 : 0;
    } catch (const SQLite::Exception& e) {
        Logger::NumError("SQLite: {}", e.what());
        return 1;
    }
}

}  // namespace numeraire::market_data_providers
