#include <numeraire/products/equity_asset_or_nothing_product.hpp>

namespace numeraire::products {

EquityAssetOrNothingProduct::EquityAssetOrNothingProduct(std::string underlying_id, const OptionType kind,
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

std::string_view EquityAssetOrNothingProduct::UnderlyingId() const { return underlying_id_; }

OptionType EquityAssetOrNothingProduct::OptionKind() const { return kind_; }

ExerciseStyle EquityAssetOrNothingProduct::Exercise() const { return exercise_; }

double EquityAssetOrNothingProduct::Strike() const { return strike_; }

const schedule::Date& EquityAssetOrNothingProduct::TradeDate() const { return trade_date_; }

const schedule::Date& EquityAssetOrNothingProduct::ExpiryDate() const { return expiry_date_; }

const schedule::Schedule* EquityAssetOrNothingProduct::PaymentSchedule() const {
    return payments_ ? &(*payments_) : nullptr;
}

}  // namespace numeraire::products
