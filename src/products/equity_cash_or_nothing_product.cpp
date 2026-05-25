#include <numeraire/products/equity_cash_or_nothing_product.hpp>

namespace numeraire::products {

EquityCashOrNothingProduct::EquityCashOrNothingProduct(std::string underlying_id, const OptionType kind,
                                                       const ExerciseStyle exercise, const double strike,
                                                       const double cash_payout_per_share,
                                                       schedule::Date trade_date, schedule::Date expiry_date,
                                                       std::optional<schedule::Schedule> payments)
        : underlying_id_(std::move(underlying_id)),
          kind_(kind),
          exercise_(exercise),
          strike_(strike),
          cash_payout_per_share_(cash_payout_per_share),
          trade_date_(trade_date),
          expiry_date_(expiry_date),
          payments_(std::move(payments)) {}

std::string_view EquityCashOrNothingProduct::UnderlyingId() const { return underlying_id_; }

OptionType EquityCashOrNothingProduct::OptionKind() const { return kind_; }

ExerciseStyle EquityCashOrNothingProduct::Exercise() const { return exercise_; }

double EquityCashOrNothingProduct::Strike() const { return strike_; }

double EquityCashOrNothingProduct::CashPayoutPerShare() const { return cash_payout_per_share_; }

const schedule::Date& EquityCashOrNothingProduct::TradeDate() const { return trade_date_; }

const schedule::Date& EquityCashOrNothingProduct::ExpiryDate() const { return expiry_date_; }

const schedule::Schedule* EquityCashOrNothingProduct::PaymentSchedule() const {
    return payments_ ? &(*payments_) : nullptr;
}

}  // namespace numeraire::products
