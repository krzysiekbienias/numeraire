#include <SQLiteCpp/SQLiteCpp.h>
#include <cpr/util.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/market_data_providers/polygon_futures_daily_eod_fetch.hpp>
#include <numeraire/market_data_providers/polygon_ingest_common.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/logger.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
using numeraire::market_data_providers::polygon_ingest::SleepSecAfterPolygonFuturesCall;

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

[[nodiscard]] bool IsOutrightSingleTicker(const std::string& ticker) {
    // CME-style: product (1–4 letters) + month letter + 1–2 year digits. Parse from the right
    // so the month letter is not swallowed into the product prefix.
    if (ticker.empty() || ticker.size() > 8) {
        return false;
    }
    for (const char c : ticker) {
        if (c == ':' || c == ' ' || c == '-' || c == '/') {
            return false;
        }
    }
    size_t i = ticker.size();
    size_t digits = 0;
    while (i > 0 && std::isdigit(static_cast<unsigned char>(ticker[i - 1]))) {
        --i;
        ++digits;
    }
    if (digits < 1 || digits > 2 || i == 0) {
        return false;
    }
    const char month = ticker[i - 1];
    static constexpr const char* kMonths = "FGHJKMNQUVXZ";
    if (std::strchr(kMonths, month) == nullptr) {
        return false;
    }
    --i;
    if (i < 1 || i > 4) {
        return false;
    }
    for (size_t j = 0; j < i; ++j) {
        if (!std::isupper(static_cast<unsigned char>(ticker[j]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string ToUpperAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

[[nodiscard]] std::int64_t NsToMs(const std::int64_t raw) {
    // Massive futures window_start is often nanoseconds; equity bars use ms.
    if (raw > 10'000'000'000'000LL) {
        return raw / 1'000'000LL;
    }
    return raw;
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> LoadContractTickers(
        SQLite::Database& db,
        const std::string& listing_as_of,
        const std::vector<std::string>& product_codes,
        const bool active_only) {
    std::ostringstream sql;
    sql << "SELECT ticker, product_code FROM futures_contract WHERE listing_as_of = ?";
    if (active_only) {
        sql << " AND active = 1";
    }
    if (!product_codes.empty()) {
        sql << " AND UPPER(product_code) IN (";
        for (size_t i = 0; i < product_codes.size(); ++i) {
            if (i > 0) {
                sql << ',';
            }
            sql << '?';
        }
        sql << ')';
    }
    sql << " ORDER BY product_code, settlement_date, ticker";

    SQLite::Statement q(db, sql.str());
    int bind = 1;
    q.bind(bind++, listing_as_of);
    for (const std::string& pc : product_codes) {
        q.bind(bind++, pc);
    }

    std::vector<std::pair<std::string, std::string>> out;
    while (q.executeStep()) {
        const std::string ticker = q.getColumn(0).getString();
        if (!IsOutrightSingleTicker(ticker)) {
            continue;
        }
        out.emplace_back(ticker, ToUpperAscii(q.getColumn(1).getString()));
    }
    return out;
}

[[nodiscard]] std::string BuildSessionAggsUrl(const std::string& base,
                                              const std::string& ticker,
                                              const std::string& window_start_gte,
                                              const std::string& window_start_lte) {
    std::ostringstream oss;
    oss << base << "/futures/v1/aggs/" << cpr::util::urlEncode(ticker)
        << "?resolution=1session&limit=50000&sort=window_start.asc";
    if (window_start_gte == window_start_lte) {
        oss << "&window_start=" << cpr::util::urlEncode(window_start_gte);
    } else {
        oss << "&window_start.gte=" << cpr::util::urlEncode(window_start_gte)
            << "&window_start.lte=" << cpr::util::urlEncode(window_start_lte);
    }
    return oss.str();
}

[[nodiscard]] int UpsertSessionBars(SQLite::Database& db,
                                    const std::string& ticker,
                                    const std::string& product_code,
                                    const nlohmann::json& results,
                                    const std::string& from_iso,
                                    const std::string& to_iso,
                                    const std::string& source_label,
                                    const std::string& ingested_at) {
    if (!results.is_array()) {
        return 0;
    }

    SQLite::Statement st(
            db,
            "INSERT INTO futures_daily_eod ("
            "ticker, product_code, as_of, session_calendar, open, high, low, close, "
            "settlement_price, currency, volume, dollar_volume, vwap, trade_count, "
            "source, timespan, provider_timestamp_utc_ms, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(ticker, as_of, timespan) DO UPDATE SET "
            "product_code=excluded.product_code, session_calendar=excluded.session_calendar, "
            "open=excluded.open, high=excluded.high, low=excluded.low, close=excluded.close, "
            "settlement_price=excluded.settlement_price, currency=excluded.currency, "
            "volume=excluded.volume, dollar_volume=excluded.dollar_volume, vwap=excluded.vwap, "
            "trade_count=excluded.trade_count, source=excluded.source, "
            "provider_timestamp_utc_ms=excluded.provider_timestamp_utc_ms, "
            "ingested_at=excluded.ingested_at");

    int n = 0;
    SQLite::Transaction txn(db);
    for (const auto& r : results) {
        if (!r.is_object()) {
            continue;
        }
        std::string as_of;
        if (r.contains("session_end_date") && r["session_end_date"].is_string()) {
            as_of = r["session_end_date"].get<std::string>();
        }
        if (as_of.empty() || as_of < from_iso || as_of > to_iso) {
            continue;
        }
        if (!r.contains("open") || !r.contains("high") || !r.contains("low") || !r.contains("close")) {
            continue;
        }
        if (!r["open"].is_number() || !r["high"].is_number() || !r["low"].is_number() ||
            !r["close"].is_number()) {
            continue;
        }

        st.bind(1, ticker);
        if (product_code.empty()) {
            st.bind(2);
        } else {
            st.bind(2, product_code);
        }
        st.bind(3, as_of);
        st.bind(4, std::string("America/Chicago"));
        st.bind(5, r["open"].get<double>());
        st.bind(6, r["high"].get<double>());
        st.bind(7, r["low"].get<double>());
        st.bind(8, r["close"].get<double>());

        if (r.contains("settlement_price") && r["settlement_price"].is_number()) {
            const double settle = r["settlement_price"].get<double>();
            if (settle != 0.0) {
                st.bind(9, settle);
            } else {
                st.bind(9);
            }
        } else {
            st.bind(9);
        }
        st.bind(10, std::string("USD"));

        double volume = 0.0;
        bool have_volume = false;
        if (r.contains("volume") && r["volume"].is_number()) {
            volume = r["volume"].get<double>();
            have_volume = true;
            st.bind(11, volume);
        } else {
            st.bind(11);
        }

        double dollar_volume = 0.0;
        bool have_dollar = false;
        if (r.contains("dollar_volume") && r["dollar_volume"].is_number()) {
            dollar_volume = r["dollar_volume"].get<double>();
            have_dollar = true;
            st.bind(12, dollar_volume);
        } else {
            st.bind(12);
        }

        if (have_volume && have_dollar && volume > 0.0) {
            st.bind(13, dollar_volume / volume);
        } else {
            st.bind(13);
        }

        if (r.contains("transactions") && r["transactions"].is_number_integer()) {
            st.bind(14, r["transactions"].get<std::int64_t>());
        } else if (r.contains("transactions") && r["transactions"].is_number()) {
            st.bind(14, static_cast<std::int64_t>(r["transactions"].get<double>()));
        } else {
            st.bind(14);
        }

        st.bind(15, source_label);
        st.bind(16, std::string("1session"));

        if (r.contains("window_start") && r["window_start"].is_number_integer()) {
            st.bind(17, NsToMs(r["window_start"].get<std::int64_t>()));
        } else if (r.contains("window_start") && r["window_start"].is_number()) {
            st.bind(17, NsToMs(static_cast<std::int64_t>(r["window_start"].get<double>())));
        } else {
            st.bind(17);
        }
        st.bind(18, ingested_at);

        st.exec();
        st.reset();
        ++n;
    }
    txn.commit();
    return n;
}

[[nodiscard]] int IngestTickerRange(SQLite::Database& db,
                                    const std::string& base_url,
                                    const char* api_key,
                                    const std::string& ticker,
                                    const std::string& product_code,
                                    const std::string& from_iso,
                                    const std::string& to_iso,
                                    const std::string& window_gte,
                                    const std::string& window_lte,
                                    const int throttle_sec) {
    const std::string source_label = DataSourceLabelForBaseUrl(base_url);
    const std::string ingested_at = IsoUtcNow();
    std::string url = BuildSessionAggsUrl(base_url, ticker, window_gte, window_lte);
    int total = 0;

    for (;;) {
        nlohmann::json j;
        std::string next_url;
        if (FetchJsonPage(url, api_key, throttle_sec, j, next_url) != 0) {
            // Soft-fail per ticker (deferred tenors often 404 / empty mid-curve).
            Logger::NumWarn("Futures aggs: HTTP/parse failure for {} — skipping.", ticker);
            return 0;
        }
        if (!j.contains("results") || !j["results"].is_array() || j["results"].empty()) {
            break;
        }
        const int chunk = UpsertSessionBars(
                db, ticker, product_code, j["results"], from_iso, to_iso, source_label, ingested_at);
        total += chunk;
        if (next_url.empty()) {
            break;
        }
        url = next_url;
    }

    if (total > 0) {
        Logger::NumInfo("Futures aggs: {} upserted {} bar(s) {}..{}.", ticker, total, from_iso, to_iso);
    }
    return 0;
}

}  // namespace

void PrintFuturesDailyEodFetchUsageLines() {
    Logger::NumError(
            "  dev_main --fetch-futures-eod-daily --from YYYY-MM-DD --to YYYY-MM-DD "
            "[--listing-as-of YYYY-MM-DD] [--product-code CL]...\n"
            "    Upsert /futures/v1/aggs 1session into `futures_daily_eod` (POLYGON_API_KEY).\n"
            "    Tickers from `futures_contract` @ listing-as-of (default: --to).\n"
            "    Session settle on D uses window_start=D-1 (Massive session calendar).\n"
            "    Throttle: NUMERAIRE_POLYGON_FUTURES_PLAN / NUMERAIRE_POLYGON_FUTURES_SLEEP_SEC (default 0).");
}

int TryRunPolygonFuturesDailyEodFetch(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool fetch_mode = false;
    std::string from_iso;
    std::string to_iso;
    std::string listing_as_of;
    std::vector<std::string> product_codes;
    bool active_only = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fetch-futures-eod-daily") == 0) {
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
        } else if (std::strcmp(argv[i], "--listing-as-of") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--listing-as-of requires YYYY-MM-DD.");
                return 1;
            }
            listing_as_of = argv[++i];
        } else if (std::strcmp(argv[i], "--product-code") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--product-code requires a code (e.g. CL).");
                return 1;
            }
            product_codes.push_back(ToUpperAscii(argv[++i]));
        } else if (std::strcmp(argv[i], "--include-inactive") == 0) {
            active_only = false;
        }
    }

    if (!fetch_mode) {
        return -1;
    }

    if (from_iso.empty() || to_iso.empty()) {
        Logger::NumError("--fetch-futures-eod-daily requires --from and --to.");
        PrintFuturesDailyEodFetchUsageLines();
        return 1;
    }
    if (!LooksIsoDate(from_iso) || !LooksIsoDate(to_iso)) {
        Logger::NumError("--from/--to must be YYYY-MM-DD.");
        return 1;
    }
    if (from_iso > to_iso) {
        Logger::NumError("--from must be <= --to.");
        return 1;
    }
    if (listing_as_of.empty()) {
        listing_as_of = to_iso;
    }
    if (!LooksIsoDate(listing_as_of)) {
        Logger::NumError("--listing-as-of must be YYYY-MM-DD.");
        return 1;
    }

    std::string window_gte;
    std::string window_lte;
    if (!IsoDateAddDays(from_iso, -1, window_gte) || !IsoDateAddDays(to_iso, -1, window_lte)) {
        Logger::NumError("Failed to compute session window_start from --from/--to.");
        return 1;
    }

    const char* key = PolygonApiKey();
    if (key == nullptr || key[0] == '\0') {
        Logger::NumError("POLYGON_API_KEY is not set (e.g. in `.env`).");
        return 1;
    }

    const std::string base = PolygonBaseUrl();
    const int throttle_sec = SleepSecAfterPolygonFuturesCall();
    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");
    Logger::NumInfo("futures_daily_eod ingest → SQLite {}", db_path.string());

    try {
        SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("PRAGMA foreign_keys = ON;");

        const auto tickers = LoadContractTickers(db, listing_as_of, product_codes, active_only);
        if (tickers.empty()) {
            Logger::NumError(
                    "No futures_contract rows for listing_as_of={} (run --fetch-futures-contracts first).",
                    listing_as_of);
            return 1;
        }

        Logger::NumInfo(
                "Futures aggs: {} contract(s) listing_as_of={} window_start {}..{} → session {}..{}.",
                tickers.size(),
                listing_as_of,
                window_gte,
                window_lte,
                from_iso,
                to_iso);

        for (const auto& [ticker, product_code] : tickers) {
            if (IngestTickerRange(db,
                                  base,
                                  key,
                                  ticker,
                                  product_code,
                                  from_iso,
                                  to_iso,
                                  window_gte,
                                  window_lte,
                                  throttle_sec) != 0) {
                return 1;
            }
        }
    } catch (const SQLite::Exception& e) {
        Logger::NumError("SQLite: {}", e.what());
        return 1;
    }

    Logger::NumInfo("--fetch-futures-eod-daily finished {}..{}.", from_iso, to_iso);
    return 0;
}

}  // namespace numeraire::market_data_providers
