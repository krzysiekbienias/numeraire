#include <numeraire/products/equity_spot_product.hpp>

namespace numeraire::products {

EquitySpotProduct::EquitySpotProduct(std::string underlying_id, schedule::Date trade_date,
                                     std::optional<schedule::Schedule> payments)
        : underlying_id_(std::move(underlying_id)),
          trade_date_(trade_date),
          payments_(std::move(payments)) {}

std::string_view EquitySpotProduct::UnderlyingId() const { return underlying_id_; }

OptionType EquitySpotProduct::OptionKind() const { return OptionType::kCall; }

ExerciseStyle EquitySpotProduct::Exercise() const { return ExerciseStyle::kEuropean; }

double EquitySpotProduct::Strike() const { return 0.0; }

const schedule::Date& EquitySpotProduct::TradeDate() const { return trade_date_; }

/// No maturity — reuse trade date so `IProduct` stays satisfied; pricers must not
/// treat this as an option expiry.
const schedule::Date& EquitySpotProduct::ExpiryDate() const { return trade_date_; }

const schedule::Schedule* EquitySpotProduct::PaymentSchedule() const {
    return payments_ ? &(*payments_) : nullptr;
}

}  // namespace numeraire::products
