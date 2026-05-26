#include <numeraire/products/equity_forward_product.hpp>

namespace numeraire::products {

EquityForwardProduct::EquityForwardProduct(std::string underlying_id, const double forward_price,
                                           schedule::Date trade_date, schedule::Date expiry_date,
                                           std::optional<schedule::Schedule> payments)
        : underlying_id_(std::move(underlying_id)),
          forward_price_(forward_price),
          trade_date_(trade_date),
          expiry_date_(expiry_date),
          payments_(std::move(payments)) {}

std::string_view EquityForwardProduct::UnderlyingId() const { return underlying_id_; }

OptionType EquityForwardProduct::OptionKind() const { return OptionType::kCall; }

ExerciseStyle EquityForwardProduct::Exercise() const { return ExerciseStyle::kEuropean; }

double EquityForwardProduct::Strike() const { return forward_price_; }

const schedule::Date& EquityForwardProduct::TradeDate() const { return trade_date_; }

const schedule::Date& EquityForwardProduct::ExpiryDate() const { return expiry_date_; }

const schedule::Schedule* EquityForwardProduct::PaymentSchedule() const {
    return payments_ ? &(*payments_) : nullptr;
}

}  // namespace numeraire::products
