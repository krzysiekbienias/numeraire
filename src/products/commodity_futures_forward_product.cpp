#include <numeraire/products/commodity_futures_forward_product.hpp>

namespace numeraire::products {

CommodityFuturesForwardProduct::CommodityFuturesForwardProduct(
        std::string contract_ticker, std::string product_code, const double forward_price,
        schedule::Date trade_date, schedule::Date expiry_date,
        std::optional<schedule::Schedule> payments)
        : contract_ticker_(std::move(contract_ticker)),
          product_code_(std::move(product_code)),
          forward_price_(forward_price),
          trade_date_(trade_date),
          expiry_date_(expiry_date),
          payments_(std::move(payments)) {}

std::string_view CommodityFuturesForwardProduct::UnderlyingId() const { return contract_ticker_; }

OptionType CommodityFuturesForwardProduct::OptionKind() const { return OptionType::kCall; }

ExerciseStyle CommodityFuturesForwardProduct::Exercise() const { return ExerciseStyle::kEuropean; }

double CommodityFuturesForwardProduct::Strike() const { return forward_price_; }

const schedule::Date& CommodityFuturesForwardProduct::TradeDate() const { return trade_date_; }

const schedule::Date& CommodityFuturesForwardProduct::ExpiryDate() const { return expiry_date_; }

const schedule::Schedule* CommodityFuturesForwardProduct::PaymentSchedule() const {
    return payments_ ? &(*payments_) : nullptr;
}

std::string_view CommodityFuturesForwardProduct::ProductCode() const { return product_code_; }

}  // namespace numeraire::products
