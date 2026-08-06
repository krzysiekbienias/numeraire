#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>

#include <optional>
#include <string>

namespace numeraire::products {

/// Cash equity **or** index spot position: mark-to-market at `IMarketData::Spot`.
/// Used for delta-hedge legs next to options in the same portfolio.
///
/// Catalog: `equity_spot` (shares) or `index_spot` (index units). No strike, no
/// natural expiry — `ExpiryDate()` mirrors `TradeDate()` for `IProduct` only;
/// the spot pricer ignores \(\tau\). `OptionKind()` / `Strike()` are unused stubs
/// (same pattern as `EquityForwardProduct`).
class EquitySpotProduct final : public core::IProduct {
   public:
    EquitySpotProduct(std::string underlying_id, schedule::Date trade_date,
                      std::optional<schedule::Schedule> payments = std::nullopt);

    [[nodiscard]] std::string_view UnderlyingId() const override;

    [[nodiscard]] OptionType OptionKind() const override;

    [[nodiscard]] ExerciseStyle Exercise() const override;

    [[nodiscard]] double Strike() const override;

    [[nodiscard]] const schedule::Date& TradeDate() const override;

    [[nodiscard]] const schedule::Date& ExpiryDate() const override;

    [[nodiscard]] const schedule::Schedule* PaymentSchedule() const override;

   private:
    std::string underlying_id_;
    schedule::Date trade_date_;
    std::optional<schedule::Schedule> payments_;
};

}  // namespace numeraire::products
