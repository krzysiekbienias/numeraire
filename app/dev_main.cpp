#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeraire/core/pricing_engine.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/database/equity_daily_eod_lookup.hpp>
#include <numeraire/database/leg_pv.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/database/sqlite_trade_leg_booking_repository.hpp>
#include <numeraire/database/sqlite_trade_leg_mtm_repository.hpp>
#include <numeraire/database/sqlite_trade_repository.hpp>
#include <numeraire/database/trade_booking_rules.hpp>
#include <numeraire/database/trade_leg_booking_update.hpp>
#include <numeraire/database/trade_leg_mtm_eod_row.hpp>
#include <numeraire/enums/model_type.hpp>
#include <numeraire/enums/position_direction.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>
#include <numeraire/market_data/market_snapshot.hpp>
#include <numeraire/market_data/static_market_data_provider.hpp>
#include <numeraire/market_data_providers/polygon_daily_eod_fetch.hpp>
#include <numeraire/market_data_providers/polygon_index_daily_eod_fetch.hpp>
#include <numeraire/market_data_providers/polygon_ingest_common.hpp>
#include <numeraire/market_data_providers/polygon_option_contract_fetch.hpp>
#include <numeraire/pricers/pricer_factory.hpp>
#include <numeraire/products/product_factory.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/env_loader.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>
#include <numeraire/utils/string.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

using numeraire::PositionDirection;
using numeraire::database::BootstrapTradeDatabaseSchema;
using numeraire::database::LookupEquityDailyClose;
using numeraire::database::SqliteTradeLegBookingRepository;
using numeraire::database::SqliteTradeLegMtmRepository;
using numeraire::database::SqliteTradeRepository;
using numeraire::database::TradeCatalogBundle;
using numeraire::database::TradeLegBookingUpdate;
using numeraire::database::TradeLegMtmEodRow;
using numeraire::schedule::ParseIsoDate;
using numeraire::market_data::MarketSnapshot;
using numeraire::market_data::StaticMarketDataProvider;
using numeraire::market_data_providers::PrintFetchUsageLines;
using numeraire::market_data_providers::PrintIndexFetchUsageLines;
using numeraire::market_data_providers::PrintOptionContractFetchUsageLines;
using numeraire::market_data_providers::TryRunPolygonDailyEodFetch;
using numeraire::market_data_providers::TryRunPolygonIndexDailyEodFetch;
using numeraire::market_data_providers::TryRunPolygonOptionContractFetch;
using numeraire::pricers::PricerFactory;
using numeraire::products::ProductFactory;
using numeraire::schedule::Act365FixedYearFraction;
using numeraire::utils::Config;
using numeraire::utils::EnvLoader;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

using numeraire::market_data_providers::polygon_ingest::LooksIsoDate;

namespace {

constexpr const char* kMtmPricingEngine = "analytic_black_scholes";

[[nodiscard]] double EnvDouble(const char* key, const double default_value) noexcept {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const double v = std::strtod(raw, &end);
    if (end == raw) {
        return default_value;
    }
    return v;
}

[[nodiscard]] int EnvInt(const char* key, const int default_value) noexcept {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const long v = std::strtol(raw, &end, 10);
    if (end == raw) {
        return default_value;
    }
    return static_cast<int>(v);
}

[[nodiscard]] bool EqualsAsciiIgnoreCase(const std::string_view a, const std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

struct DevMainMarketQuotesConfig {
    enum SpotSourceKind { kEnv, kDb };

    SpotSourceKind spot_source{kEnv};
    double env_fallback_spot{240.0};
    std::string as_of_iso;
    int equity_eod_adjusted{1};
};

[[nodiscard]] DevMainMarketQuotesConfig LoadDevMainMarketQuotesConfig(const std::optional<std::string>& cli_as_of,
                                                                      const bool require_valuation_date) {
    DevMainMarketQuotesConfig c;
    const char* const src_raw = std::getenv("NUMERAIRE_DEV_SPOT_SOURCE");
    if (src_raw != nullptr && src_raw[0] != '\0') {
        const std::string_view sv{src_raw};
        if (EqualsAsciiIgnoreCase(sv, "db")) {
            c.spot_source = DevMainMarketQuotesConfig::kDb;
        } else if (EqualsAsciiIgnoreCase(sv, "env")) {
            c.spot_source = DevMainMarketQuotesConfig::kEnv;
        } else {
            throw numeraire::ValidationError(std::string{"NUMERAIRE_DEV_SPOT_SOURCE must be 'env' or 'db' (got: "} +
                                             src_raw + ")");
        }
    }

    c.env_fallback_spot = EnvDouble("NUMERAIRE_DEV_SPOT", 240.0);
    if (cli_as_of.has_value()) {
        c.as_of_iso = *cli_as_of;
    } else if (require_valuation_date) {
        if (const char* ao = std::getenv("NUMERAIRE_DEV_AS_OF"); ao != nullptr && ao[0] != '\0') {
            c.as_of_iso = ao;
        }
    }
    const int adj_raw = EnvInt("NUMERAIRE_DEV_SPOT_ADJUSTED", 1);
    c.equity_eod_adjusted = (adj_raw != 0) ? 1 : 0;

    if (require_valuation_date && (c.as_of_iso.empty() || !LooksIsoDate(c.as_of_iso))) {
        throw numeraire::ValidationError(
                "MTM pricing requires a valuation date: pass `--as-of YYYY-MM-DD` or set NUMERAIRE_DEV_AS_OF.");
    }
    return c;
}

struct PricingArgvScan {
    std::optional<std::string> as_of;
    bool price_booking{false};
    std::vector<std::string> positional;
};

/// Strips `--as-of` / `--price-booking` so remaining tokens follow the legacy layout.
[[nodiscard]] PricingArgvScan ScanPricingArgv(const int argc, char** argv) {
    PricingArgvScan out;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--as-of") == 0) {
            if (out.price_booking) {
                throw numeraire::ValidationError("--as-of cannot be used with --price-booking.");
            }
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                throw numeraire::ValidationError("--as-of requires YYYY-MM-DD.");
            }
            out.as_of = std::string(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--price-booking") == 0) {
            if (out.as_of.has_value()) {
                throw numeraire::ValidationError("--price-booking cannot be used with --as-of.");
            }
            if (out.price_booking) {
                throw numeraire::ValidationError("--price-booking specified more than once.");
            }
            out.price_booking = true;
            continue;
        }
        out.positional.emplace_back(argv[i]);
    }
    if (out.price_booking && out.as_of.has_value()) {
        throw numeraire::ValidationError("--price-booking cannot be used with --as-of.");
    }
    return out;
}

[[nodiscard]] std::string MakeBatchRunId() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::array<char, 32> buf{};
    if (std::strftime(buf.data(), buf.size(), "run-%Y%m%dT%H%M%SZ", &tm_buf) == 0U) {
        return "run-unknown";
    }
    return std::string(buf.data());
}

[[nodiscard]] bool ShouldPersistMtmEod(const DevMainMarketQuotesConfig& mq) {
    return !mq.as_of_iso.empty() && LooksIsoDate(mq.as_of_iso);
}

[[nodiscard]] std::string BuildMtmRemarks(const DevMainMarketQuotesConfig& mq) {
    std::ostringstream oss;
    if (mq.spot_source == DevMainMarketQuotesConfig::kDb) {
        oss << "SPOT_DB;";
    } else {
        oss << "SPOT_ENV;";
    }
    oss << "IV_ENV;R_ENV;Q_ENV";
    return oss.str();
}

[[nodiscard]] double GreekOrZero(const std::optional<double>& g) noexcept {
    return g.has_value() ? *g : 0.0;
}

[[nodiscard]] double DividendYieldForUnderlying(const MarketSnapshot& snap, const std::string& underlying_id) {
    const auto it = snap.dividend_yields.find(underlying_id);
    if (it == snap.dividend_yields.end()) {
        return 0.0;
    }
    return it->second;
}

[[nodiscard]] double BookNpvAndPersistMtmForBundle(const TradeCatalogBundle& bundle,
                                                   const numeraire::core::IMarketData& mkt,
                                                   const MarketSnapshot& snap,
                                                   const numeraire::core::IPricer& pricer,
                                                   SqliteTradeLegMtmRepository* mtm_repo,
                                                   const DevMainMarketQuotesConfig& mq,
                                                   const std::string& batch_run_id,
                                                   const std::string& remarks) {
    double total_npv = 0.0;
    for (const auto& row : bundle.legs) {
        const auto product = ProductFactory::MakeFromEquityCatalog(row.product, row.equity, &bundle.trade);
        if (product == nullptr) {
            throw numeraire::PersistenceError("ProductFactory returned null for leg " + row.leg.leg_id);
        }
        const double contract_size = row.equity.contract_size;
        numeraire::database::RequirePositiveContractSize(contract_size, row.leg.leg_id);
        numeraire::database::RequirePositiveQuantity(row.leg.quantity, row.leg.leg_id);

        numeraire::core::PricingResult result = numeraire::core::PricingEngine::Price(*product, pricer, mkt);

        if (!result.Npv().has_value()) {
            throw numeraire::PersistenceError("Pricer returned no NPV for leg " + row.leg.leg_id);
        }

        const double unit_npv = *result.Npv();
        const double pv_total =
                numeraire::database::LegPvTotal(row.leg.direction, row.leg.quantity, contract_size, unit_npv);
        total_npv += pv_total;

        if (mtm_repo == nullptr) {
            continue;
        }

        const std::string& underlying = row.equity.underlying_id;
        const double spot_used = mkt.Spot(underlying);

        const double years_to_maturity = Act365FixedYearFraction(mkt.ValuationDate(), product->ExpiryDate());

        TradeLegMtmEodRow mtm{};
        mtm.as_of = mq.as_of_iso;
        mtm.trade_id = bundle.trade.trade_id;
        mtm.leg_id = row.leg.leg_id;
        mtm.batch_run_id = batch_run_id;
        mtm.underlying_spot = spot_used;
        mtm.risk_free_rate = snap.risk_free_rate;
        mtm.dividend_yield = DividendYieldForUnderlying(snap, underlying);
        mtm.implied_vol_used = snap.flat_implied_volatility;
        mtm.years_to_maturity = years_to_maturity;
        mtm.numeraire_currency = row.equity.currency.empty() ? "USD" : row.equity.currency;
        mtm.pv_unit = unit_npv;
        mtm.pv_total = pv_total;

        if (const auto& greeks = result.Greeks()) {
            const double delta_unit = GreekOrZero(greeks->delta);
            const double gamma_unit = GreekOrZero(greeks->gamma);
            const double vega_unit = GreekOrZero(greeks->vega);
            const double theta_unit = GreekOrZero(greeks->theta);
            const double rho_unit = GreekOrZero(greeks->rho);
            mtm.delta = delta_unit;
            mtm.gamma = gamma_unit;
            mtm.vega = vega_unit;
            mtm.theta = theta_unit;
            mtm.rho = rho_unit;
            mtm.delta_total =
                    numeraire::database::LegDeltaTotal(row.leg.direction, row.leg.quantity, contract_size, delta_unit);
            mtm.gamma_total =
                    numeraire::database::LegGammaTotal(row.leg.direction, row.leg.quantity, contract_size, gamma_unit);
            mtm.vega_total =
                    numeraire::database::LegVegaTotal(row.leg.direction, row.leg.quantity, contract_size, vega_unit);
            mtm.theta_total =
                    numeraire::database::LegThetaTotal(row.leg.direction, row.leg.quantity, contract_size, theta_unit);
            mtm.rho_total =
                    numeraire::database::LegRhoTotal(row.leg.direction, row.leg.quantity, contract_size, rho_unit);
        }

        mtm.pricing_engine = kMtmPricingEngine;
        mtm.remarks = remarks;
        mtm_repo->Upsert(mtm);

        Logger::NumInfo(
                "MTM persisted leg_id={} as_of={} pv_unit={} pv_total={} batch_run_id={} "
                "(official + archive).",
                mtm.leg_id,
                mtm.as_of,
                mtm.pv_unit,
                mtm.pv_total,
                *mtm.batch_run_id);
    }
    return total_npv;
}

void PrintUsage() {
    const char* const msg =
            "dev_main — price trade(s) from SQLite, or ingest Polygon EOD bars.\n"
            "Usage:\n"
            "  dev_main <trade_id>              Price one trade (e.g. TRD_SAMPLE_001).\n"
            "  dev_main --all                   Price every trade_id in `trades` (sorted).\n"
            "  dev_main --trades-json <path>    Price ids from JSON: [\"TRD_1\",...] or "
            "{\"trade_ids\":[\"TRD_1\",...]}.\n"
            "  dev_main --fetch-eod-daily ...     Ingest daily aggs into `equity_daily_eod` "
            "(see --help).\n"
            "  dev_main --fetch-index-eod-daily ... Ingest index daily aggs into `index_daily_eod` "
            "(see --help).\n"
            "  dev_main --fetch-option-contracts ... Ingest options reference into `option_contract` "
            "(see --help).\n"
            "  dev_main --as-of YYYY-MM-DD <trade_id> | --all | --trades-json <path>   (MTM; flags in any order)\n"
            "  dev_main --price-booking <trade_id> | --all | --trades-json <path>   (book PENDING trades on "
            "trade_date)\n"
            "If no args: NUMERAIRE_DEV_TRADE_ID from the environment (single trade, MTM mode).\n"
            "MTM requires --as-of or NUMERAIRE_DEV_AS_OF. Booking forbids --as-of; ValuationDate = trade_date.\n"
            "Booking: status PENDING → LIVE when every leg execution_price > 0. MTM requires LIVE + booked legs.\n"
            "Pricing spot: NUMERAIRE_DEV_SPOT_SOURCE=env|db (`db` reads equity_daily_eod.close on that date). "
            "Rate/vol from NUMERAIRE_DEV_RATE / NUMERAIRE_DEV_VOL.\n"
            "MTM persist: writes one row per leg into trade_leg_mtm_eod (same batch_run_id for the whole run).\n"
            "Options: --help, -h";
    Logger::NumError("{}", msg);
    PrintFetchUsageLines();
    PrintIndexFetchUsageLines();
    PrintOptionContractFetchUsageLines();
}

[[nodiscard]] std::vector<std::string> LoadTradeIdsFromJsonFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw numeraire::ValidationError("cannot read trades JSON: " + path.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    const nlohmann::json j = nlohmann::json::parse(oss.str(), nullptr, false);
    if (j.is_discarded()) {
        throw numeraire::ValidationError("invalid JSON: " + path.string());
    }

    std::vector<std::string> out;
    const nlohmann::json* arr = nullptr;
    if (j.is_array()) {
        arr = &j;
    } else if (j.is_object() && j.contains("trade_ids")) {
        const auto& t = j.at("trade_ids");
        if (!t.is_array()) {
            throw numeraire::ValidationError("trade_ids must be a JSON array in: " + path.string());
        }
        arr = &t;
    } else {
        throw numeraire::ValidationError("JSON mustale  be [\"TRD_1\",...] or {\"trade_ids\":[\"TRD_1\",...]} : " +
                                         path.string());
    }

    out.reserve(arr->size());
    for (const auto& el : *arr) {
        if (!el.is_string()) {
            throw numeraire::ValidationError("each trade id must be a string in: " + path.string());
        }
        std::string s = el.get<std::string>();
        if (!s.empty()) {
            out.push_back(std::move(s));
        }
    }
    if (out.empty()) {
        throw numeraire::ValidationError("no trade ids in JSON: " + path.string());
    }
    return out;
}

void FillUnderlyingSpotsAcrossBundles(MarketSnapshot& snap,
                                      const std::vector<TradeCatalogBundle>& bundles,
                                      const std::filesystem::path& db_path_file,
                                      const DevMainMarketQuotesConfig& mq) {
    std::unordered_set<std::string> seen;
    for (const TradeCatalogBundle& bundle : bundles) {
        for (const auto& row : bundle.legs) {
            const std::string& u = row.equity.underlying_id;
            if (seen.contains(u)) {
                continue;
            }
            seen.insert(u);

            if (mq.spot_source == DevMainMarketQuotesConfig::kEnv) {
                snap.spots[u] = mq.env_fallback_spot;
                Logger::NumInfo("Underlying {} spot={} from env (NUMERAIRE_DEV_SPOT, NUMERAIRE_DEV_SPOT_SOURCE=env).",
                                u,
                                mq.env_fallback_spot);
                continue;
            }

            const std::optional<double> close =
                    LookupEquityDailyClose(db_path_file.string(), u, mq.as_of_iso, mq.equity_eod_adjusted);
            if (!close.has_value()) {
                throw numeraire::ValidationError(
                        "equity_daily_eod: missing row for ticker=" + u + " as_of=" + mq.as_of_iso +
                        " adjusted=" + std::to_string(mq.equity_eod_adjusted) +
                        " — ingest daily bars before pricing with NUMERAIRE_DEV_SPOT_SOURCE=db.");
            }
            snap.spots[u] = *close;
            Logger::NumInfo("Underlying {} spot={} from equity_daily_eod (as_of={}, adjusted={}).",
                            u,
                            *close,
                            mq.as_of_iso,
                            mq.equity_eod_adjusted);
        }
    }
}

void FillDividendYieldsAcrossBundles(MarketSnapshot& snap,
                                     const std::vector<TradeCatalogBundle>& bundles,
                                     const double dividend_yield) {
    if (dividend_yield == 0.0) {
        return;
    }
    for (const TradeCatalogBundle& bundle : bundles) {
        for (const auto& row : bundle.legs) {
            const std::string& u = row.equity.underlying_id;
            if (!snap.dividend_yields.contains(u)) {
                snap.dividend_yields[u] = dividend_yield;
            }
        }
    }
}

[[nodiscard]] int RunWithTradeIds(const SqliteTradeRepository& repo,
                                  const std::filesystem::path& db_path,
                                  std::vector<std::string> trade_ids,
                                  DevMainMarketQuotesConfig mq) {
    if (trade_ids.empty()) {
        Logger::NumError("No trades to price (empty list).");
        return 1;
    }

    std::vector<TradeCatalogBundle> bundles;
    bundles.reserve(trade_ids.size());
    for (const std::string& tid : trade_ids) {
        bundles.push_back(repo.GetCatalogForTrade(tid));
        if (bundles.back().legs.empty()) {
            Logger::NumError("Trade {} has no legs in database.", tid);
            return 1;
        }
        numeraire::database::RequireTradeLiveForMtm(bundles.back().trade);
        numeraire::database::RequireAllLegsBookedForMtm(bundles.back());
        numeraire::database::RequireMtmAsOfNotBeforeTradeDate(mq.as_of_iso, bundles.back().trade);
    }

    MarketSnapshot snap;
    snap.valuation_date = numeraire::schedule::ParseIsoDate(mq.as_of_iso);
    snap.risk_free_rate = EnvDouble("NUMERAIRE_DEV_RATE", 0.03);
    snap.flat_implied_volatility = EnvDouble("NUMERAIRE_DEV_VOL", 0.20);
    const double q = EnvDouble("NUMERAIRE_DEV_DIV_YIELD", 0.0);

    FillUnderlyingSpotsAcrossBundles(snap, bundles, db_path, mq);
    FillDividendYieldsAcrossBundles(snap, bundles, q);

    Logger::NumInfo("Quotes: risk_free_rate={} (env) flat_iv={} (env) dividends={} (env); spot_source={} as_of={}.",
                    snap.risk_free_rate,
                    snap.flat_implied_volatility,
                    q,
                    mq.spot_source == DevMainMarketQuotesConfig::kDb ? "db" : "env",
                    mq.spot_source == DevMainMarketQuotesConfig::kDb ? mq.as_of_iso : std::string{"n/a"});

    const MarketSnapshot snap_for_mtm = snap;
    StaticMarketDataProvider market(std::move(snap));
    const auto mkt_handle = market.CreateMarketData();
    auto pricer = PricerFactory::Make(numeraire::PricingEngineType::kAnalytic, numeraire::ModelType::kBlackScholes);

    const bool persist_mtm = ShouldPersistMtmEod(mq);
    std::optional<SqliteTradeLegMtmRepository> mtm_repo;
    std::string batch_run_id;
    std::string mtm_remarks;
    if (persist_mtm) {
        mtm_repo.emplace(db_path.string());
        batch_run_id = MakeBatchRunId();
        mtm_remarks = BuildMtmRemarks(mq);
        Logger::NumInfo("MTM EOD persist enabled as_of={} batch_run_id={}.", mq.as_of_iso, batch_run_id);
    } else {
        Logger::NumInfo(
                "MTM EOD persist skipped: set --as-of YYYY-MM-DD or NUMERAIRE_DEV_AS_OF to write "
                "trade_leg_mtm_eod.");
    }

    SqliteTradeLegMtmRepository* mtm_ptr = persist_mtm ? &*mtm_repo : nullptr;

    for (size_t i = 0; i < trade_ids.size(); ++i) {
        const std::string& tid = trade_ids[i];
        const auto& bundle = bundles[i];
        Logger::NumInfo("Loaded trade_id={} legs={} portfolio_id={} first_leg_product={} underlying={}.",
                        tid,
                        bundle.legs.size(),
                        bundle.trade.portfolio_id,
                        bundle.legs.front().leg.product_id,
                        bundle.legs.front().equity.underlying_id);

        const double total_npv = BookNpvAndPersistMtmForBundle(
                bundle, *mkt_handle, snap_for_mtm, *pricer, mtm_ptr, mq, batch_run_id, mtm_remarks);
        Logger::NumInfo("Book NPV (summed legs, Black–Scholes analytic) trade_id={} → {}.", tid, total_npv);
    }

    Logger::NumInfo("Priced {} trade(s).{}", trade_ids.size(), persist_mtm ? " MTM rows written." : "");
    return 0;
}

[[nodiscard]] std::vector<TradeLegBookingUpdate> PriceBundleForBooking(const TradeCatalogBundle& bundle,
                                                                       const numeraire::core::IMarketData& mkt,
                                                                       const numeraire::core::IPricer& pricer) {
    numeraire::database::RequireValuationDateEqualsTradeDate(mkt.ValuationDate(), bundle.trade);

    std::vector<TradeLegBookingUpdate> updates;
    updates.reserve(bundle.legs.size());

    for (const auto& row : bundle.legs) {
        const auto product = ProductFactory::MakeFromEquityCatalog(row.product, row.equity, &bundle.trade);
        if (product == nullptr) {
            throw numeraire::PersistenceError("ProductFactory returned null for leg " + row.leg.leg_id);
        }
        numeraire::database::RequirePositiveContractSize(row.equity.contract_size, row.leg.leg_id);
        numeraire::database::RequirePositiveQuantity(row.leg.quantity, row.leg.leg_id);

        const numeraire::core::PricingResult result = numeraire::core::PricingEngine::Price(*product, pricer, mkt);
        if (!result.Npv().has_value()) {
            throw numeraire::PersistenceError("Pricer returned no NPV for leg " + row.leg.leg_id);
        }

        const double unit_npv = *result.Npv();
        updates.push_back(TradeLegBookingUpdate{.leg_id = row.leg.leg_id, .execution_price = unit_npv});

        Logger::NumInfo(
                "Booking priced leg_id={} trade_date valuation pv_unit={} (ValuationDate equals trade_date).",
                row.leg.leg_id,
                unit_npv);
    }
    return updates;
}

[[nodiscard]] int RunBookingWithTradeIds(const SqliteTradeRepository& repo,
                                           const std::filesystem::path& db_path,
                                           std::vector<std::string> trade_ids,
                                           DevMainMarketQuotesConfig mq) {
    if (trade_ids.empty()) {
        Logger::NumError("No trades to book (empty list).");
        return 1;
    }

    SqliteTradeLegBookingRepository booking_repo(db_path.string());
    auto pricer = PricerFactory::Make(numeraire::PricingEngineType::kAnalytic, numeraire::ModelType::kBlackScholes);

    for (const std::string& tid : trade_ids) {
        TradeCatalogBundle bundle = repo.GetCatalogForTrade(tid);
        if (bundle.legs.empty()) {
            Logger::NumError("Trade {} has no legs in database.", tid);
            return 1;
        }

        numeraire::database::RequireTradePendingForBooking(bundle.trade);
        mq.as_of_iso = numeraire::database::RequireTradeDateIso(bundle.trade);

        MarketSnapshot snap;
        snap.valuation_date = ParseIsoDate(mq.as_of_iso);
        numeraire::database::RequireValuationDateEqualsTradeDate(snap.valuation_date, bundle.trade);
        snap.risk_free_rate = EnvDouble("NUMERAIRE_DEV_RATE", 0.03);
        snap.flat_implied_volatility = EnvDouble("NUMERAIRE_DEV_VOL", 0.20);
        const double q = EnvDouble("NUMERAIRE_DEV_DIV_YIELD", 0.0);

        FillUnderlyingSpotsAcrossBundles(snap, {bundle}, db_path, mq);
        FillDividendYieldsAcrossBundles(snap, {bundle}, q);

        Logger::NumInfo(
                "Booking quotes trade_id={} trade_date={} spot_source={} risk_free_rate={} flat_iv={}.",
                tid,
                mq.as_of_iso,
                mq.spot_source == DevMainMarketQuotesConfig::kDb ? "db" : "env",
                snap.risk_free_rate,
                snap.flat_implied_volatility);

        StaticMarketDataProvider market(std::move(snap));
        const auto mkt_handle = market.CreateMarketData();

        const std::vector<TradeLegBookingUpdate> updates = PriceBundleForBooking(bundle, *mkt_handle, *pricer);
        booking_repo.ApplyTradeBooking(tid, updates, std::nullopt);

        bool all_positive = true;
        for (const TradeLegBookingUpdate& u : updates) {
            if (u.execution_price <= 0.0) {
                all_positive = false;
                break;
            }
        }
        if (all_positive) {
            booking_repo.SetTradeStatus(tid, std::string{numeraire::database::kTradeStatusLive});
            Logger::NumInfo("Trade {} promoted to LIVE (all legs execution_price > 0).", tid);
        } else {
            Logger::NumInfo(
                    "Trade {} remains PENDING (at least one leg execution_price <= 0 after booking run).",
                    tid);
        }
    }

    Logger::NumInfo("Booked {} trade(s) on trade_date valuation.", trade_ids.size());
    return 0;
}

[[nodiscard]] int ParseArgsAndRun(int argc,
                                  char** argv,
                                  const SqliteTradeRepository& repo,
                                  const std::filesystem::path& db_path) {
    PricingArgvScan scan;
    try {
        scan = ScanPricingArgv(argc, argv);
    } catch (const numeraire::ValidationError& ex) {
        Logger::NumError("{}", ex.what());
        return 1;
    }

    DevMainMarketQuotesConfig mq;
    try {
        mq = LoadDevMainMarketQuotesConfig(scan.as_of, !scan.price_booking);
    } catch (const numeraire::ValidationError& ex) {
        Logger::NumError("{}", ex.what());
        return 1;
    }

    const std::vector<std::string>& tok = scan.positional;
    if (!tok.empty() && (tok[0] == "--help" || tok[0] == "-h")) {
        PrintUsage();
        return 0;
    }

    if (tok.empty()) {
        if (const char* v = std::getenv("NUMERAIRE_DEV_TRADE_ID"); v != nullptr && v[0] != '\0') {
            if (scan.price_booking) {
                return RunBookingWithTradeIds(repo, db_path, {std::string(v)}, mq);
            }
            return RunWithTradeIds(repo, db_path, {std::string(v)}, mq);
        }
        Logger::NumError(
                "No trade specified: pass <trade_id>, --all, or --trades-json <path>, or set "
                "NUMERAIRE_DEV_TRADE_ID.");
        PrintUsage();
        return 1;
    }

    if (tok[0] == "--all") {
        if (tok.size() != 1U) {
            Logger::NumError("`--all` must be the only pricing argument.");
            PrintUsage();
            return 1;
        }
        if (scan.price_booking) {
            return RunBookingWithTradeIds(repo, db_path, repo.ListAllTradeIds(), mq);
        }
        return RunWithTradeIds(repo, db_path, repo.ListAllTradeIds(), mq);
    }

    if (tok[0] == "--trades-json") {
        if (tok.size() < 2U || tok[1].empty()) {
            Logger::NumError("--trades-json requires a file path.");
            return 1;
        }
        if (tok.size() > 2U) {
            Logger::NumError("--trades-json expects exactly one JSON path.");
            return 1;
        }
        if (scan.price_booking) {
            return RunBookingWithTradeIds(repo, db_path, LoadTradeIdsFromJsonFile(tok[1]), mq);
        }
        return RunWithTradeIds(repo, db_path, LoadTradeIdsFromJsonFile(tok[1]), mq);
    }

    if (!tok[0].empty() && tok[0][0] == '-') {
        Logger::NumError("Unknown option: {}.", tok[0]);
        PrintUsage();
        return 1;
    }

    if (tok.size() != 1U) {
        Logger::NumError("Expected exactly one trade_id positional argument.");
        PrintUsage();
        return 1;
    }
    if (scan.price_booking) {
        return RunBookingWithTradeIds(repo, db_path, {tok[0]}, mq);
    }
    return RunWithTradeIds(repo, db_path, {tok[0]}, mq);
}

}  // namespace

int main(const int argc, char** argv) {
    EnvLoader env;
    env.LoadFromFile(".env");
    env.ApplyToEnvironment();

    Logger::Init();
    try {
        const Config cfg = Config::Load("configs/default.json");
        Logger::NumInfo("dev_main (config version={}).", cfg.RequireString("version"));

        const int index_fetch_rc = TryRunPolygonIndexDailyEodFetch(argc, argv, cfg);
        if (index_fetch_rc >= 0) {
            return index_fetch_rc;
        }
        const int fetch_rc = TryRunPolygonDailyEodFetch(argc, argv, cfg);
        if (fetch_rc >= 0) {
            return fetch_rc;
        }
        const int option_contract_rc = TryRunPolygonOptionContractFetch(argc, argv, cfg);
        if (option_contract_rc >= 0) {
            return option_contract_rc;
        }

        const std::filesystem::path db_path = ResolveDatabasePath(cfg);
        BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");
        const SqliteTradeRepository repo(db_path.string());
        Logger::NumInfo("SQLite trade repository at {}.", db_path.string());

        const int rc = ParseArgsAndRun(argc, argv, repo, db_path);
        if (rc != 0) {
            return rc;
        }

    } catch (const numeraire::NumeraireException& ex) {
        Logger::NumError("{}", ex.what());
        return 1;
    }

    Logger::NumInfo("dev_main done (DEV_MAIN_BUILD).");
    return 0;
}
