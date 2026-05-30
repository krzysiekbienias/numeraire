#include <numeraire/market_data/curved_rate_market_data.hpp>

#include <numeraire/market_data/discount_curve_rate.hpp>
#include <numeraire/schedule/date.hpp>

#include <utility>

namespace numeraire::market_data {

CurvedRateMarketData::CurvedRateMarketData(std::unique_ptr<core::IMarketData> inner,
                                           database::DiscountCurveEodRead curve,
                                           const double flat_risk_free_rate_fallback)
        : inner_(std::move(inner)),
          curve_(std::move(curve)),
          flat_risk_free_rate_fallback_(flat_risk_free_rate_fallback),
          representative_risk_free_rate_(RepresentativeRiskFreeRate(curve_, flat_risk_free_rate_fallback)) {}

std::unique_ptr<core::IMarketData> CurvedRateMarketData::MaybeWrap(
        std::unique_ptr<core::IMarketData> inner,
        const std::optional<database::DiscountCurveEodRead>& curve,
        const double flat_risk_free_rate_fallback) {
    if (!curve.has_value()) {
        return inner;
    }
    return std::make_unique<CurvedRateMarketData>(std::move(inner), *curve, flat_risk_free_rate_fallback);
}

const schedule::Date& CurvedRateMarketData::ValuationDate() const { return inner_->ValuationDate(); }

double CurvedRateMarketData::Spot(const std::string_view underlying_id) const {
    return inner_->Spot(underlying_id);
}

double CurvedRateMarketData::RiskFreeRate() const { return representative_risk_free_rate_; }

double CurvedRateMarketData::RiskFreeRateForTenor(const double time_to_expiry_years) const {
    return numeraire::market_data::RiskFreeRateForTenor(curve_, flat_risk_free_rate_fallback_, time_to_expiry_years);
}

double CurvedRateMarketData::DividendYield(const std::string_view underlying_id) const {
    return inner_->DividendYield(underlying_id);
}

double CurvedRateMarketData::ImpliedVolatility(const std::string_view underlying_id,
                                               const double strike,
                                               const double time_to_expiry_years,
                                               const OptionType option_kind) const {
    return inner_->ImpliedVolatility(underlying_id, strike, time_to_expiry_years, option_kind);
}

}  // namespace numeraire::market_data
