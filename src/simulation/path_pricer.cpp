#include <numeraire/simulation/path_pricer.hpp>

#include <numeraire/core/pricing_engine.hpp>
#include <numeraire/database/leg_pv.hpp>
#include <numeraire/database/sqlite_trade_repository.hpp>
#include <numeraire/database/trade_booking_rules.hpp>
#include <numeraire/products/product_factory.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/path_pricing_rules.hpp>
#include <numeraire/simulation/scenario_slice_market_data.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::simulation {
namespace {

[[nodiscard]] schedule::Date RequireLegExpiryDate(const database::TradeLegCatalogRow& row) {
    if (!row.equity.expiry_date.has_value() || row.equity.expiry_date->empty()) {
        throw ValidationError("LoadPathPricingLegsForPortfolio: leg " + row.leg.leg_id +
                              " missing expiry_date.");
    }
    return schedule::ParseIsoDate(*row.equity.expiry_date);
}

}  // namespace

std::unordered_map<std::string, std::size_t> BuildFactorIndexByUnderlying(
        const std::span<const std::string> factor_underlying_ids) {
    std::unordered_map<std::string, std::size_t> out;
    out.reserve(factor_underlying_ids.size());
    for (std::size_t i = 0; i < factor_underlying_ids.size(); ++i) {
        out.emplace(factor_underlying_ids[i], i);
    }
    return out;
}

std::vector<PathPricingLegEntry> LoadPathPricingLegsForPortfolio(
        const std::string& database_file_path,
        const std::string& portfolio_id,
        const std::unordered_map<std::string, std::size_t>& factor_by_underlying) {
    database::SqliteTradeRepository repo(database_file_path);
    const std::vector<std::string> trade_ids = repo.ListLiveTradeIdsForPortfolio(portfolio_id);
    if (trade_ids.empty()) {
        throw ValidationError("LoadPathPricingLegsForPortfolio: no LIVE trades for portfolio_id=" +
                              portfolio_id);
    }

    std::vector<PathPricingLegEntry> legs;
    for (const std::string& trade_id : trade_ids) {
        const database::TradeCatalogBundle bundle = repo.GetCatalogForTrade(trade_id);
        if (bundle.trade.portfolio_id != portfolio_id) {
            continue;
        }
        database::RequireTradeLiveForMtm(bundle.trade);

        for (const database::TradeLegCatalogRow& row : bundle.legs) {
            const schedule::Date expiry_date = RequireLegExpiryDate(row);

            const std::string& underlying = row.equity.underlying_id;
            const auto factor_it = factor_by_underlying.find(underlying);
            if (factor_it == factor_by_underlying.end()) {
                throw ValidationError("LoadPathPricingLegsForPortfolio: leg " + row.leg.leg_id +
                                      " underlying " + underlying +
                                      " is not in calibration factor set for portfolio " + portfolio_id);
            }

            database::RequirePositiveContractSize(row.equity.contract_size, row.leg.leg_id);
            database::RequirePositiveQuantity(row.leg.quantity, row.leg.leg_id);

            auto product = products::ProductFactory::MakeFromEquityCatalog(row.product, row.equity,
                                                                           &bundle.trade);
            if (product == nullptr) {
                throw ValidationError("LoadPathPricingLegsForPortfolio: ProductFactory returned null for leg " +
                                      row.leg.leg_id);
            }

            PathPricingLegEntry entry;
            entry.leg_id = row.leg.leg_id;
            entry.underlying_id = underlying;
            entry.factor_index = factor_it->second;
            entry.direction = row.leg.direction;
            entry.quantity = row.leg.quantity;
            entry.contract_size = row.equity.contract_size;
            entry.expiry_date = expiry_date;
            entry.product = std::move(product);
            legs.push_back(std::move(entry));
        }
    }

    if (legs.empty()) {
        throw ValidationError("LoadPathPricingLegsForPortfolio: no LIVE legs for portfolio_id=" + portfolio_id);
    }
    return legs;
}

void PricePortfolioAlongPaths(const ScenarioBuffer& buffer,
                              const ExposureTimeGrid& time_grid,
                              const std::span<const std::string> factor_underlying_ids,
                              const std::vector<PathPricingLegEntry>& legs,
                              const PathPricingQuotes& quotes,
                              const core::IPricer& pricer,
                              LegPathPvBuffer& out_pv,
                              std::vector<std::string>& out_leg_ids) {
    if (legs.empty()) {
        throw ValidationError("PricePortfolioAlongPaths: legs must not be empty.");
    }
    if (buffer.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("PricePortfolioAlongPaths: buffer steps must match time_grid.NumSteps().");
    }
    if (factor_underlying_ids.size() != buffer.NumFactors()) {
        throw ValidationError("PricePortfolioAlongPaths: factor_underlying_ids length must match NumFactors().");
    }
    if (out_pv.NumLegs() != legs.size() || out_pv.NumSteps() != buffer.NumSteps() ||
        out_pv.NumPaths() != buffer.NumPaths()) {
        throw ValidationError("PricePortfolioAlongPaths: out_pv dimensions must match legs/buffer/grid.");
    }

    const std::unordered_map<std::string, std::size_t> factor_by_underlying =
            BuildFactorIndexByUnderlying(factor_underlying_ids);

    out_leg_ids.clear();
    out_leg_ids.reserve(legs.size());
    for (const PathPricingLegEntry& leg : legs) {
        out_leg_ids.push_back(leg.leg_id);
    }

    ScenarioSliceMarketData market(buffer, time_grid, factor_by_underlying, quotes);

    for (std::size_t step = 0; step < time_grid.NumSteps(); ++step) {
        const schedule::Date& node_date = time_grid.nodes[step].date;
        for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
            market.SetSlice(step, path);
            for (std::size_t leg_index = 0; leg_index < legs.size(); ++leg_index) {
                const PathPricingLegEntry& leg = legs[leg_index];
                if (!IsLegActiveOnGridNode(node_date, leg.expiry_date)) {
                    out_pv.At(leg_index, step, path) = 0.0;
                    continue;
                }
                const core::PricingResult priced =
                        core::PricingEngine::Price(*leg.product, pricer, market);
                if (!priced.Npv().has_value()) {
                    throw ValidationError("PricePortfolioAlongPaths: pricer returned no NPV for leg " +
                                          leg.leg_id);
                }
                out_pv.At(leg_index, step, path) = database::LegPvTotal(
                        leg.direction, leg.quantity, leg.contract_size, *priced.Npv());
            }
        }
    }
}

}  // namespace numeraire::simulation
