#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>

#include <optional>
#include <string>

namespace numeraire::products {

/// Uncollateralized **forward on a listed commodity futures** contract.
/// Locks delivery price `Strike()` \(K\) against the dated futures
/// (`UnderlyingId()` = contract ticker, e.g. `CLX6`). Analytic PV:
/// \(e^{-r\tau}(F_{t,T}-K)\). `OptionKind()` is unused for pricing.
class CommodityFuturesForwardProduct final : public core::IProduct {
   public:
    CommodityFuturesForwardProduct(std::string contract_ticker, std::string product_code,
                                   double forward_price, schedule::Date trade_date,
                                   schedule::Date expiry_date,
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
    double forward_price_{};
    schedule::Date trade_date_;
    schedule::Date expiry_date_;
    std::optional<schedule::Schedule> payments_;
};

}  // namespace numeraire::products
