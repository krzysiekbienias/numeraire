#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeraire/core/pricing_engine.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/database/sqlite_trade_repository.hpp>
#include <numeraire/enums/model_type.hpp>
#include <numeraire/enums/position_direction.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>
#include <numeraire/market_data/market_snapshot.hpp>
#include <numeraire/market_data/static_market_data_provider.hpp>
#include <numeraire/pricers/pricer_factory.hpp>
#include <numeraire/products/product_factory.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/env_loader.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using numeraire::PositionDirection;
using numeraire::database::BootstrapTradeDatabaseSchema;
using numeraire::database::SqliteTradeRepository;
using numeraire::database::TradeCatalogBundle;
using numeraire::market_data::MarketSnapshot;
using numeraire::market_data::StaticMarketDataProvider;
using numeraire::pricers::PricerFactory;
using numeraire::products::ProductFactory;
using numeraire::utils::Config;
using numeraire::utils::EnvLoader;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

namespace {

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

[[nodiscard]] double PositionSign(const PositionDirection direction) noexcept {
    return direction == PositionDirection::kShort ? -1.0 : 1.0;
}

void PrintUsage() {
    const char* const msg =
            "dev_main — price trade(s) from SQLite (Black–Scholes analytic per leg).\n"
            "Usage:\n"
            "  dev_main <trade_id>              Price one trade (e.g. TRD_SAMPLE_001).\n"
            "  dev_main --all                   Price every trade_id in `trades` (sorted).\n"
            "  dev_main --trades-json <path>    Price ids from JSON: [\"TRD_1\",...] or "
            "{\"trade_ids\":[\"TRD_1\",...]}.\n"
            "If no args: NUMERAIRE_DEV_TRADE_ID from the environment (single trade).\n"
            "Options: --help, -h";
    Logger::NumError("{}", msg);
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
        throw numeraire::ValidationError("JSON must be [\"TRD_1\",...] or {\"trade_ids\":[\"TRD_1\",...]} : " +
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

void UpsertMarketQuotesForBundle(MarketSnapshot& snap,
                                 const TradeCatalogBundle& bundle,
                                 const double default_spot,
                                 const double dividend_yield) {
    for (const auto& row : bundle.legs) {
        const std::string& u = row.equity.underlying_id;
        if (snap.spots.find(u) == snap.spots.end()) {
            snap.spots[u] = default_spot;
        }
        if (dividend_yield != 0.0 && snap.dividend_yields.find(u) == snap.dividend_yields.end()) {
            snap.dividend_yields[u] = dividend_yield;
        }
    }
}

[[nodiscard]] double BookNpvForBundle(const TradeCatalogBundle& bundle,
                                      const numeraire::core::IMarketData& mkt,
                                      const numeraire::core::IPricer& pricer) {
    double total_npv = 0.0;
    for (const auto& row : bundle.legs) {
        const auto product = ProductFactory::MakeFromEquityCatalog(row.product, row.equity, &bundle.trade);
        if (product == nullptr) {
            throw numeraire::PersistenceError("ProductFactory returned null for leg " + row.leg.leg_id);
        }
        numeraire::core::PricingResult result = numeraire::core::PricingEngine::Price(*product, pricer, mkt);

        if (!result.Npv().has_value()) {
            throw numeraire::PersistenceError("Pricer returned no NPV for leg " + row.leg.leg_id);
        }

        const double unit_npv = *result.Npv();
        total_npv += PositionSign(row.leg.direction) * row.leg.quantity * unit_npv;
    }
    return total_npv;
}

[[nodiscard]] int RunWithTradeIds(const SqliteTradeRepository& repo, std::vector<std::string> trade_ids) {
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
    }

    MarketSnapshot snap;
    snap.risk_free_rate = EnvDouble("NUMERAIRE_DEV_RATE", 0.03);
    snap.flat_implied_volatility = EnvDouble("NUMERAIRE_DEV_VOL", 0.20);
    const double q = EnvDouble("NUMERAIRE_DEV_DIV_YIELD", 0.0);
    const double default_spot = EnvDouble("NUMERAIRE_DEV_SPOT", 240.0);
    for (const auto& bundle : bundles) {
        UpsertMarketQuotesForBundle(snap, bundle, default_spot, q);
    }

    StaticMarketDataProvider market(std::move(snap));
    const auto mkt_handle = market.CreateMarketData();
    auto pricer = PricerFactory::Make(numeraire::PricingEngineType::kAnalytic, numeraire::ModelType::kBlackScholes);

    for (size_t i = 0; i < trade_ids.size(); ++i) {
        const std::string& tid = trade_ids[i];
        const auto& bundle = bundles[i];
        Logger::NumInfo("Loaded trade_id={} legs={} portfolio_id={} first_leg_product={} underlying={}.",
                        tid,
                        bundle.legs.size(),
                        bundle.trade.portfolio_id,
                        bundle.legs.front().leg.product_id,
                        bundle.legs.front().equity.underlying_id);

        const double total_npv = BookNpvForBundle(bundle, *mkt_handle, *pricer);
        Logger::NumInfo("Book NPV (summed legs, Black–Scholes analytic) trade_id={} → {}.", tid, total_npv);
    }

    Logger::NumInfo("Priced {} trade(s).", trade_ids.size());
    return 0;
}

[[nodiscard]] int ParseArgsAndRun(int argc, char** argv, const SqliteTradeRepository& repo) {
    if (argc >= 2) {
        if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            PrintUsage();
            return 0;
        }
        if (std::strcmp(argv[1], "--all") == 0) {
            return RunWithTradeIds(repo, repo.ListAllTradeIds());
        }
        if (std::strcmp(argv[1], "--trades-json") == 0) {
            if (argc < 3 || argv[2] == nullptr || argv[2][0] == '\0') {
                Logger::NumError("--trades-json requires a file path.");
                return 1;
            }
            return RunWithTradeIds(repo, LoadTradeIdsFromJsonFile(argv[2]));
        }
        if (argv[1][0] == '-') {
            Logger::NumError("Unknown option: {}.", argv[1]);
            PrintUsage();
            return 1;
        }
        return RunWithTradeIds(repo, {std::string(argv[1])});
    }

    if (const char* v = std::getenv("NUMERAIRE_DEV_TRADE_ID"); v != nullptr && v[0] != '\0') {
        return RunWithTradeIds(repo, {std::string(v)});
    }

    Logger::NumError(
            "No trade specified: pass <trade_id>, --all, or --trades-json <path>, or set "
            "NUMERAIRE_DEV_TRADE_ID.");
    PrintUsage();
    return 1;
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

        const std::filesystem::path db_path = ResolveDatabasePath(cfg);
        BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");
        const SqliteTradeRepository repo(db_path.string());
        Logger::NumInfo("SQLite trade repository at {}.", db_path.string());

        const int rc = ParseArgsAndRun(argc, argv, repo);
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
