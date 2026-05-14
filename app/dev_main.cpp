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

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

using numeraire::PositionDirection;
using numeraire::database::BootstrapTradeDatabaseSchema;
using numeraire::database::SqliteTradeRepository;
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

[[nodiscard]] std::string ResolveTradeId(const int argc, char** argv) {
    if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0') {
        return std::string(argv[1]);
    }
    if (const char* v = std::getenv("NUMERAIRE_DEV_TRADE_ID"); v != nullptr && v[0] != '\0') {
        return std::string(v);
    }
    return {};
}

[[nodiscard]] double PositionSign(const PositionDirection direction) noexcept {
    return direction == PositionDirection::kShort ? -1.0 : 1.0;
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

        const std::string trade_id = ResolveTradeId(argc, argv);
        if (trade_id.empty()) {
            Logger::NumError(
                    "Missing trade id: pass as first argument (e.g. ./build/dev_main TRD_001) or set "
                    "NUMERAIRE_DEV_TRADE_ID in .env.");
            return 1;
        }

        const auto bundle = repo.GetCatalogForTrade(trade_id);
        Logger::NumInfo("Loaded trade_id={} product_id={} underlying={}.", bundle.trade.trade_id,
                bundle.trade.product_id, bundle.equity.underlying_id);

        const auto product =
                ProductFactory::MakeFromEquityCatalog(bundle.product, bundle.equity, &bundle.trade);
        if (product == nullptr) {
            Logger::NumError("ProductFactory returned null for {}.", trade_id);
            return 1;
        }

        MarketSnapshot snap;
        snap.spots[std::string(product->UnderlyingId())] =
                EnvDouble("NUMERAIRE_DEV_SPOT", 240.0);
        snap.risk_free_rate = EnvDouble("NUMERAIRE_DEV_RATE", 0.03);
        snap.flat_implied_volatility = EnvDouble("NUMERAIRE_DEV_VOL", 0.20);
        const double q = EnvDouble("NUMERAIRE_DEV_DIV_YIELD", 0.0);
        if (q != 0.0) {
            snap.dividend_yields[std::string(product->UnderlyingId())] = q;
        }

        StaticMarketDataProvider market(std::move(snap));
        const auto mkt_handle = market.CreateMarketData();

        const auto pricer =
                PricerFactory::Make(numeraire::PricingEngineType::kAnalytic, numeraire::ModelType::kBlackScholes);

        numeraire::core::PricingResult result =
                numeraire::core::PricingEngine::Price(*product, *pricer, *mkt_handle);

        if (!result.Npv().has_value()) {
            Logger::NumError("Pricer returned no NPV for {}.", trade_id);
            return 1;
        }

        const double unit_npv = *result.Npv();
        const double total_npv = PositionSign(bundle.trade.direction) * bundle.trade.quantity * unit_npv;

        Logger::NumInfo(
                "Unit NPV={} ({}, Black–Scholes analytic); scaled by qty={} direction={} → book NPV={}.", unit_npv,
                bundle.equity.underlying_id, bundle.trade.quantity,
                bundle.trade.direction == PositionDirection::kShort ? "SHORT" : "LONG", total_npv);

    } catch (const numeraire::NumeraireException& ex) {
        Logger::NumError("{}", ex.what());
        return 1;
    }

    Logger::NumInfo("dev_main done (DEV_MAIN_BUILD).");
    return 0;
}
