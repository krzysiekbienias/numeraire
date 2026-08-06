#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/products/equity_asset_or_nothing_product.hpp>
#include <numeraire/products/equity_cash_or_nothing_product.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/quant/black_scholes_vanilla.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::pricers {

namespace {

[[nodiscard]] core::PricingGreeks ToCoreGreeks(const quant::EuropeanVanillaGreeks& greeks) {
    core::PricingGreeks out;
    out.delta = greeks.delta;
    out.gamma = greeks.gamma;
    out.vega = greeks.vega;
    out.theta = greeks.theta;
    out.rho = greeks.rho;
    return out;
}

[[nodiscard]] core::PricingResult PriceVanilla(const products::VanillaEquityOptionProduct& vanilla,
                                               const core::IMarketData& market) {
    if (vanilla.Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("AnalyticBlackScholesEquityPricer supports European exercise only");
    }

    const double time_to_expiry =
            schedule::Act365FixedYearFraction(market.ValuationDate(), vanilla.ExpiryDate());
    const double spot = market.Spot(vanilla.UnderlyingId());
    const double strike = vanilla.Strike();
    const double r = market.RiskFreeRateForTenor(time_to_expiry);
    const double q = market.DividendYield(vanilla.UnderlyingId());

    core::PricingResult result;

    if (time_to_expiry <= 0.0) {
        const double pv_unit = quant::EuropeanVanillaIntrinsic(vanilla.OptionKind(), spot, strike);
        result.SetNpv(pv_unit);
        return result;
    }

    const double vol = market.ImpliedVolatility(vanilla.UnderlyingId(), strike, time_to_expiry, vanilla.OptionKind());

    const double pv_unit =
            quant::EuropeanVanillaPrice(vanilla.OptionKind(), spot, strike, r, q, vol, time_to_expiry);
    result.SetNpv(pv_unit);

    // A deterministic payoff has no sensitivities to report.
    if (vol > 0.0) {
        result.SetGreeks(ToCoreGreeks(quant::EuropeanVanillaAllGreeks(
                vanilla.OptionKind(), spot, strike, r, q, vol, time_to_expiry)));
    }
    return result;
}

[[nodiscard]] core::PricingResult PriceAssetOrNothing(const products::EquityAssetOrNothingProduct& aon,
                                                      const core::IMarketData& market) {
    if (aon.Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("AnalyticBlackScholesEquityPricer supports European exercise only");
    }

    const double time_to_expiry = schedule::Act365FixedYearFraction(market.ValuationDate(), aon.ExpiryDate());
    const double spot = market.Spot(aon.UnderlyingId());
    const double strike = aon.Strike();
    const double r = market.RiskFreeRateForTenor(time_to_expiry);
    const double q = market.DividendYield(aon.UnderlyingId());

    core::PricingResult result;

    if (time_to_expiry <= 0.0) {
        const double pv_unit = quant::AssetOrNothingIntrinsic(aon.OptionKind(), spot, strike);
        result.SetNpv(pv_unit);
        return result;
    }

    const double vol = market.ImpliedVolatility(aon.UnderlyingId(), strike, time_to_expiry, aon.OptionKind());

    const double pv_unit =
            quant::AssetOrNothingPrice(aon.OptionKind(), spot, strike, r, q, vol, time_to_expiry);
    result.SetNpv(pv_unit);
    return result;
}

[[nodiscard]] core::PricingResult PriceCashOrNothing(const products::EquityCashOrNothingProduct& con,
                                                     const core::IMarketData& market) {
    if (con.Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("AnalyticBlackScholesEquityPricer supports European exercise only");
    }

    const double time_to_expiry = schedule::Act365FixedYearFraction(market.ValuationDate(), con.ExpiryDate());
    const double spot = market.Spot(con.UnderlyingId());
    const double strike = con.Strike();
    const double cash_payout = con.CashPayoutPerShare();
    const double r = market.RiskFreeRateForTenor(time_to_expiry);
    const double q = market.DividendYield(con.UnderlyingId());

    core::PricingResult result;

    if (time_to_expiry <= 0.0) {
        const double pv_unit =
                quant::CashOrNothingIntrinsic(con.OptionKind(), spot, strike, cash_payout);
        result.SetNpv(pv_unit);
        return result;
    }

    const double vol = market.ImpliedVolatility(con.UnderlyingId(), strike, time_to_expiry, con.OptionKind());

    const double pv_unit = quant::CashOrNothingPrice(con.OptionKind(), spot, strike, cash_payout, r, q,
                                                     vol, time_to_expiry);
    result.SetNpv(pv_unit);
    return result;
}

}  // namespace

numeraire::PricingEngineType AnalyticBlackScholesEquityPricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticBlackScholesEquityPricer::Price(const core::IProduct& product,
                                                            const core::IMarketData& market) const {
    if (const auto* vanilla = dynamic_cast<const products::VanillaEquityOptionProduct*>(&product)) {
        return PriceVanilla(*vanilla, market);
    }
    if (const auto* aon = dynamic_cast<const products::EquityAssetOrNothingProduct*>(&product)) {
        return PriceAssetOrNothing(*aon, market);
    }
    if (const auto* con = dynamic_cast<const products::EquityCashOrNothingProduct*>(&product)) {
        return PriceCashOrNothing(*con, market);
    }
    throw ValidationError(
            "AnalyticBlackScholesEquityPricer requires VanillaEquityOptionProduct, "
            "EquityAssetOrNothingProduct, or EquityCashOrNothingProduct");
}

}  // namespace numeraire::pricers
