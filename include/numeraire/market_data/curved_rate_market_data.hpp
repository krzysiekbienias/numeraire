#pragma once

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/database/discount_curve_eod_read.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>

#include <memory>
#include <optional>

namespace numeraire::market_data {

/// Decorator: overrides risk-free rate queries using a bootstrapped discount curve.
/// All other `IMarketData` calls delegate to `inner`.
class CurvedRateMarketData final : public core::IMarketData {
   public:
    CurvedRateMarketData(std::unique_ptr<core::IMarketData> inner,
                         database::DiscountCurveEodRead curve,
                         double flat_risk_free_rate_fallback);

    [[nodiscard]] static std::unique_ptr<core::IMarketData> MaybeWrap(
            std::unique_ptr<core::IMarketData> inner,
            const std::optional<database::DiscountCurveEodRead>& curve,
            double flat_risk_free_rate_fallback);

    [[nodiscard]] const schedule::Date& ValuationDate() const override;

    [[nodiscard]] double Spot(std::string_view underlying_id) const override;

    [[nodiscard]] double RiskFreeRate() const override;

    [[nodiscard]] double RiskFreeRateForTenor(double time_to_expiry_years) const override;

    [[nodiscard]] double DividendYield(std::string_view underlying_id) const override;

    [[nodiscard]] double ImpliedVolatility(std::string_view underlying_id,
                                           double strike,
                                           double time_to_expiry_years,
                                           OptionType option_kind) const override;

   private:
    std::unique_ptr<core::IMarketData> inner_;
    database::DiscountCurveEodRead curve_;
    double flat_risk_free_rate_fallback_{0.0};
    double representative_risk_free_rate_{0.0};
};

}  // namespace numeraire::market_data
