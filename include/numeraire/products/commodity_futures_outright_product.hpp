#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>

#include <optional>
#include <string>

namespace numeraire::products {

/// Listed commodity **futures outright**: mark-to-market at the contract
/// settlement (via `IMarketData::Spot` keyed by `contract_ticker`).
///
/// `UnderlyingId()` returns the **contract ticker** (e.g. `CLX6`), not the
/// product family (`CL`). Listed futures are daily-margined — no discount /
/// carry in the analytic outright pricer. `OptionKind()` / `Strike()` are stubs
/// (same pattern as `EquitySpotProduct`).
class CommodityFuturesOutrightProduct final : public core::IProduct {
   public:
    CommodityFuturesOutrightProduct(std::string contract_ticker, std::string product_code,
                                    schedule::Date trade_date, schedule::Date expiry_date,
                                    std::optional<schedule::Schedule> payments = std::nullopt);

    [[nodiscard]] std::string_view UnderlyingId() const override;

    [[nodiscard]] OptionType OptionKind() const override;

    [[nodiscard]] ExerciseStyle Exercise() const override;

    [[nodiscard]] double Strike() const override;

    [[nodiscard]] const schedule::Date& TradeDate() const override;

    [[nodiscard]] const schedule::Date& ExpiryDate() const override;

    [[nodiscard]] const schedule::Schedule* PaymentSchedule() const override;

    [[nodiscard]] std::string_view ProductCode() const;

   private:
    std::string contract_ticker_;
    std::string product_code_;
    schedule::Date trade_date_;
    schedule::Date expiry_date_;
    std::optional<schedule::Schedule> payments_;
};

}  // namespace numeraire::products
