#include <numeraire/products/vanilla_equity_option_product.hpp>

namespace numeraire::products {

VanillaEquityOptionProduct::VanillaEquityOptionProduct(std::string underlying_id, const OptionType kind,
                                                       const ExerciseStyle exercise, const double strike,
                                                       schedule::Date trade_date, schedule::Date expiry_date,
                                                       std::optional<schedule::Schedule> payments)
        : underlying_id_(std::move(underlying_id)),
          kind_(kind),
          exercise_(exercise),
          strike_(strike),
          trade_date_(trade_date),
          expiry_date_(expiry_date),
          payments_(std::move(payments)) {}

std::string_view VanillaEquityOptionProduct::UnderlyingId() const { return underlying_id_; }

OptionType VanillaEquityOptionProduct::OptionKind() const { return kind_; }

ExerciseStyle VanillaEquityOptionProduct::Exercise() const { return exercise_; }

double VanillaEquityOptionProduct::Strike() const { return strike_; }

const schedule::Date& VanillaEquityOptionProduct::TradeDate() const { return trade_date_; }

const schedule::Date& VanillaEquityOptionProduct::ExpiryDate() const { return expiry_date_; }

const schedule::Schedule* VanillaEquityOptionProduct::PaymentSchedule() const {
    return payments_ ? &(*payments_) : nullptr;
}

}  // namespace numeraire::products
