#include <numeraire/simulation/path_pricer.hpp>

#include <numeraire/database/futures_pillar_returns.hpp>

#include <numeraire/core/pricing_engine.hpp>
#include <numeraire/database/leg_pv.hpp>
#include <numeraire/database/sqlite_trade_repository.hpp>
#include <numeraire/database/trade_booking_rules.hpp>
#include <numeraire/products/commodity_futures_forward_product.hpp>
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

/// Booking denormalizes the contract's settlement date onto the leg, so the reference
/// table is not needed here. Either column carries it.
[[nodiscard]] schedule::Date RequireFuturesSettlementDate(const database::TradeLegCatalogRow& row) {
    const std::optional<std::string>& settlement = row.commodity->settlement_date;
    if (settlement.has_value() && !settlement->empty()) {
        return schedule::ParseIsoDate(*settlement);
    }
    return RequireLegExpiryDate(row);
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
            const bool is_futures = row.commodity.has_value();

            const std::string underlying =
                    is_futures ? row.commodity->contract_ticker : row.equity.underlying_id;

            // A futures leg binds to whatever the calibration actually carries. A Gabillon
            // fit simulates the dated contract itself, so the ticker is a factor outright;
            // a GBM pillar strip only knows constant maturities, and the leg has to be
            // interpolated onto it per grid node by the curve resolver.
            std::string factor_key = underlying;
            if (is_futures && !factor_by_underlying.contains(underlying)) {
                factor_key = database::FuturesPillarFactorId(row.commodity->product_code, 1);
            }

            const schedule::Date expiry_date =
                    is_futures ? RequireFuturesSettlementDate(row) : RequireLegExpiryDate(row);

            const auto factor_it = factor_by_underlying.find(factor_key);
            if (factor_it == factor_by_underlying.end()) {
                std::string message = "LoadPathPricingLegsForPortfolio: leg ";
                message.append(row.leg.leg_id).append(" underlying ").append(underlying);
                message.append(" maps to factor ").append(factor_key);
                message.append(" which is not in the calibration factor set for portfolio ");
                message.append(portfolio_id);
                throw ValidationError(message);
            }

            database::RequirePositiveContractSize(row.equity.contract_size, row.leg.leg_id);
            database::RequirePositiveQuantity(row.leg.quantity, row.leg.leg_id);

            auto product = products::ProductFactory::MakeFromCatalogLeg(row, &bundle.trade);
            if (product == nullptr) {
                throw ValidationError("LoadPathPricingLegsForPortfolio: ProductFactory returned null for leg " +
                                      row.leg.leg_id);
            }

            PathPricingLegEntry entry;
            entry.leg_id = row.leg.leg_id;
            entry.trade_id = bundle.trade.trade_id;
            entry.underlying_id = underlying;
            entry.factor_index = factor_it->second;
            entry.direction = row.leg.direction;
            entry.quantity = row.leg.quantity;
            entry.contract_size = row.equity.contract_size;
            entry.expiry_date = expiry_date;
            entry.product = std::move(product);
            if (is_futures) {
                entry.contract = CommodityContractRef{
                        .contract_ticker = row.commodity->contract_ticker,
                        .product_code = row.commodity->product_code,
                        .settlement_date = expiry_date,
                };
                // Listed outright marks at full F; exposure is the move since fill.
                // A true forward already has K in PV — do not subtract execution_price.
                const bool is_commodity_forward =
                        dynamic_cast<const products::CommodityFuturesForwardProduct*>(entry.product.get()) !=
                        nullptr;
                entry.pv_unit_offset = is_commodity_forward ? 0.0 : row.leg.execution_price;
            }
            legs.push_back(std::move(entry));
        }
    }

    if (legs.empty()) {
        throw ValidationError("LoadPathPricingLegsForPortfolio: no LIVE legs for portfolio_id=" + portfolio_id);
    }
    return legs;
}

std::vector<CommodityContractRef> CollectCommodityContracts(const std::vector<PathPricingLegEntry>& legs) {
    std::vector<CommodityContractRef> out;
    for (const PathPricingLegEntry& leg : legs) {
        if (leg.contract.has_value()) {
            out.push_back(*leg.contract);
        }
    }
    return out;
}

void PricePortfolioAlongPaths(const ScenarioBuffer& buffer,
                              const ExposureTimeGrid& time_grid,
                              const std::span<const std::string> factor_underlying_ids,
                              const std::vector<PathPricingLegEntry>& legs,
                              const PathPricingMarketConfig& market_config,
                              const core::IPricer& pricer,
                              LegPathPvBuffer& out_pv,
                              std::vector<std::string>& out_leg_ids,
                              const CommodityCurveResolver* commodity_curves) {
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

    if (commodity_curves == nullptr) {
        for (const PathPricingLegEntry& leg : legs) {
            // A contract simulated in its own right needs no resolver; only one standing
            // in for a pillar strip does.
            if (leg.contract.has_value() && !factor_by_underlying.contains(leg.underlying_id)) {
                throw ValidationError("PricePortfolioAlongPaths: leg " + leg.leg_id +
                                      " is a futures leg priced off constant-maturity pillars but no "
                                      "commodity curve resolver was supplied.");
            }
        }
    }

    ScenarioSliceMarketData market(buffer, time_grid, factor_by_underlying, market_config, commodity_curves);

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
                        leg.direction, leg.quantity, leg.contract_size, *priced.Npv() - leg.pv_unit_offset);
            }
        }
    }
}

}  // namespace numeraire::simulation
