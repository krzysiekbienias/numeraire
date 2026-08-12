#include <SQLiteCpp/SQLiteCpp.h>
#include <cpr/util.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/market_data_providers/polygon_futures_contract_fetch.hpp>
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
using numeraire::market_data_providers::polygon_ingest::DataSourceLabelForBaseUrl;
using numeraire::market_data_providers::polygon_ingest::FetchJsonPage;
using numeraire::market_data_providers::polygon_ingest::IsoUtcNow;
using numeraire::market_data_providers::polygon_ingest::LooksIsoDate;
using numeraire::market_data_providers::polygon_ingest::PolygonApiKey;
using numeraire::market_data_providers::polygon_ingest::PolygonBaseUrl;
using numeraire::market_data_providers::polygon_ingest::SleepSecAfterPolygonFuturesCall;

namespace {

[[nodiscard]] bool IsOutrightSingleTicker(const std::string& ticker) {
    // CME-style: CLU6 / NGU26 — product (1–4 letters) + month letter + 1–2 year digits.
    // Reject combos / options / spaces (CLM6-C, CL:F…, butterflies with hyphens).
    // Parse from the right: trailing digits → month letter → product prefix
    // (month is also A–Z, so a left-to-right "all letters" scan would swallow it).
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
    --i;  // product code length
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

[[nodiscard]] std::vector<std::string> LoadUniverseProductCodes(SQLite::Database& db) {
    std::vector<std::string> out;
    SQLite::Statement q(db,
                        "SELECT provider_symbol FROM universe_instrument "
                        "WHERE is_active = 1 AND asset_class = 'COMMODITY' "
                        "AND (ingest_futures_eod = 1 OR ingest_futures_product = 1) "
                        "ORDER BY ingest_priority, provider_symbol");
    while (q.executeStep()) {
        std::string code = ToUpperAscii(q.getColumn(0).getString());
        if (!code.empty()) {
            out.push_back(std::move(code));
        }
    }
    return out;
}

[[nodiscard]] std::string BuildContractsUrl(const std::string& base,
                                            const std::string& product_code,
                                            const std::string& as_of,
                                            const bool active_only,
                                            const std::string& type_filter) {
    std::ostringstream oss;
    oss << base << "/futures/v1/contracts?product_code=" << cpr::util::urlEncode(product_code)
        << "&date=" << cpr::util::urlEncode(as_of) << "&limit=1000&sort=ticker.asc";
    if (active_only) {
        oss << "&active=true";
    }
    if (!type_filter.empty()) {
        oss << "&type=" << cpr::util::urlEncode(type_filter);
    }
    return oss.str();
}

[[nodiscard]] int OptionalBool01(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) {
        return -1;  // sentinel → NULL bind
    }
    if (j[key].is_boolean()) {
        return j[key].get<bool>() ? 1 : 0;
    }
    if (j[key].is_number_integer()) {
        return j[key].get<int>() != 0 ? 1 : 0;
    }
    return -1;
}

void BindOptionalText(SQLite::Statement& st, const int idx, const nlohmann::json& j, const char* key) {
    if (j.contains(key) && j[key].is_string()) {
        st.bind(idx, j[key].get<std::string>());
    } else {
        st.bind(idx);
    }
}

void BindOptionalReal(SQLite::Statement& st, const int idx, const nlohmann::json& j, const char* key) {
    if (j.contains(key) && j[key].is_number()) {
        st.bind(idx, j[key].get<double>());
    } else {
        st.bind(idx);
    }
}

void BindOptionalInt(SQLite::Statement& st, const int idx, const nlohmann::json& j, const char* key) {
    if (j.contains(key) && j[key].is_number_integer()) {
        st.bind(idx, j[key].get<int>());
    } else if (j.contains(key) && j[key].is_number()) {
        st.bind(idx, static_cast<int>(j[key].get<double>()));
    } else {
        st.bind(idx);
    }
}

[[nodiscard]] int UpsertContractRows(SQLite::Database& db,
                                     const nlohmann::json& results,
                                     const std::string& listing_as_of,
                                     const std::string& fallback_product_code,
                                     const std::string& source_label,
                                     const std::string& ingested_at) {
    if (!results.is_array()) {
        return 0;
    }

    SQLite::Statement st(
            db,
            "INSERT INTO futures_contract ("
            "ticker, listing_as_of, product_code, name, active, type, trading_venue, "
            "group_code, first_trade_date, last_trade_date, settlement_date, "
            "days_to_maturity, trade_tick_size, settlement_tick_size, spread_tick_size, "
            "min_order_quantity, max_order_quantity, source, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(ticker, listing_as_of) DO UPDATE SET "
            "product_code=excluded.product_code, name=excluded.name, active=excluded.active, "
            "type=excluded.type, trading_venue=excluded.trading_venue, group_code=excluded.group_code, "
            "first_trade_date=excluded.first_trade_date, last_trade_date=excluded.last_trade_date, "
            "settlement_date=excluded.settlement_date, days_to_maturity=excluded.days_to_maturity, "
            "trade_tick_size=excluded.trade_tick_size, settlement_tick_size=excluded.settlement_tick_size, "
            "spread_tick_size=excluded.spread_tick_size, min_order_quantity=excluded.min_order_quantity, "
            "max_order_quantity=excluded.max_order_quantity, source=excluded.source, "
            "ingested_at=excluded.ingested_at");

    int n = 0;
    SQLite::Transaction txn(db);
    for (const auto& r : results) {
        if (!r.is_object() || !r.contains("ticker") || !r["ticker"].is_string()) {
            continue;
        }
        const std::string ticker = r["ticker"].get<std::string>();
        if (!IsOutrightSingleTicker(ticker)) {
            continue;
        }

        std::string product_code = fallback_product_code;
        if (r.contains("product_code") && r["product_code"].is_string()) {
            const std::string pc = ToUpperAscii(r["product_code"].get<std::string>());
            if (!pc.empty()) {
                product_code = pc;
            }
        }

        st.bind(1, ticker);
        st.bind(2, listing_as_of);
        st.bind(3, product_code);
        BindOptionalText(st, 4, r, "name");
        const int active = OptionalBool01(r, "active");
        if (active < 0) {
            st.bind(5);
        } else {
            st.bind(5, active);
        }
        BindOptionalText(st, 6, r, "type");
        BindOptionalText(st, 7, r, "trading_venue");
        BindOptionalText(st, 8, r, "group_code");
        BindOptionalText(st, 9, r, "first_trade_date");
        BindOptionalText(st, 10, r, "last_trade_date");
        BindOptionalText(st, 11, r, "settlement_date");
        BindOptionalInt(st, 12, r, "days_to_maturity");
        BindOptionalReal(st, 13, r, "trade_tick_size");
        BindOptionalReal(st, 14, r, "settlement_tick_size");
        BindOptionalReal(st, 15, r, "spread_tick_size");
        BindOptionalInt(st, 16, r, "min_order_quantity");
        BindOptionalInt(st, 17, r, "max_order_quantity");
        st.bind(18, source_label);
        st.bind(19, ingested_at);

        st.exec();
        st.reset();
        ++n;
    }
    txn.commit();
    return n;
}

[[nodiscard]] int IngestProductContracts(SQLite::Database& db,
                                         const std::string& base_url,
                                         const char* api_key,
                                         const std::string& product_code,
                                         const std::string& as_of,
                                         const bool active_only,
                                         const std::string& type_filter,
                                         const int throttle_sec) {
    const std::string source_label = DataSourceLabelForBaseUrl(base_url);
    const std::string ingested_at = IsoUtcNow();
    std::string url = BuildContractsUrl(base_url, product_code, as_of, active_only, type_filter);
    int total = 0;

    for (;;) {
        nlohmann::json j;
        std::string next_url;
        if (FetchJsonPage(url, api_key, throttle_sec, j, next_url) != 0) {
            return 1;
        }
        if (!j.contains("results") || !j["results"].is_array() || j["results"].empty()) {
            Logger::NumWarn("Futures contracts: no results for {} as_of={}.", product_code, as_of);
            break;
        }
        const int chunk =
                UpsertContractRows(db, j["results"], as_of, product_code, source_label, ingested_at);
        total += chunk;
        Logger::NumInfo("Futures contracts: {} +{} outright row(s) (running {}).",
                        product_code,
                        chunk,
                        total);
        if (next_url.empty()) {
            break;
        }
        url = next_url;
    }

    Logger::NumInfo("Futures contracts: done {} total {} row(s) @ {}.", product_code, total, as_of);
    return 0;
}

}  // namespace

void PrintFuturesContractFetchUsageLines() {
    Logger::NumError(
            "  dev_main --fetch-futures-contracts --as-of YYYY-MM-DD [--product-code CL]...\n"
            "    Upsert Massive /futures/v1/contracts into `futures_contract` (POLYGON_API_KEY).\n"
            "    Default products: active COMMODITY universe with ingest_futures_* flags.\n"
            "    Filters: type=single + outright ticker pattern (CLU6 / NGU26).\n"
            "    Throttle: NUMERAIRE_POLYGON_FUTURES_PLAN / NUMERAIRE_POLYGON_FUTURES_SLEEP_SEC (default 0).");
}

int TryRunPolygonFuturesContractFetch(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool fetch_mode = false;
    std::string as_of;
    std::vector<std::string> product_codes;
    bool active_only = true;
    std::string type_filter = "single";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fetch-futures-contracts") == 0) {
            fetch_mode = true;
        } else if (std::strcmp(argv[i], "--as-of") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--as-of requires YYYY-MM-DD.");
                return 1;
            }
            as_of = argv[++i];
        } else if (std::strcmp(argv[i], "--product-code") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--product-code requires a code (e.g. CL).");
                return 1;
            }
            product_codes.push_back(ToUpperAscii(argv[++i]));
        } else if (std::strcmp(argv[i], "--include-inactive") == 0) {
            active_only = false;
        } else if (std::strcmp(argv[i], "--type") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--type requires single|combo|all.");
                return 1;
            }
            const std::string t = ToUpperAscii(argv[++i]);
            if (t == "ALL" || t == "*") {
                type_filter.clear();
            } else if (t == "SINGLE" || t == "COMBO") {
                type_filter = t == "SINGLE" ? "single" : "combo";
            } else {
                Logger::NumError("--type must be single, combo, or all.");
                return 1;
            }
        }
    }

    if (!fetch_mode) {
        return -1;
    }

    if (as_of.empty() || !LooksIsoDate(as_of)) {
        Logger::NumError("--fetch-futures-contracts requires --as-of YYYY-MM-DD.");
        PrintFuturesContractFetchUsageLines();
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
    Logger::NumInfo("futures_contract ingest → SQLite {}", db_path.string());

    try {
        SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("PRAGMA foreign_keys = ON;");

        if (product_codes.empty()) {
            product_codes = LoadUniverseProductCodes(db);
        }
        if (product_codes.empty()) {
            Logger::NumError("No COMMODITY products in universe_instrument (seed scope first).");
            return 1;
        }

        for (const std::string& code : product_codes) {
            if (IngestProductContracts(db, base, key, code, as_of, active_only, type_filter, throttle_sec) !=
                0) {
                return 1;
            }
        }
    } catch (const SQLite::Exception& e) {
        Logger::NumError("SQLite: {}", e.what());
        return 1;
    }

    Logger::NumInfo("--fetch-futures-contracts finished for {} product(s) @ {}.", product_codes.size(), as_of);
    return 0;
}

}  // namespace numeraire::market_data_providers
