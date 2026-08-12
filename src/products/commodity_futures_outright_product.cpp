#include <numeraire/products/commodity_futures_outright_product.hpp>

namespace numeraire::products {

CommodityFuturesOutrightProduct::CommodityFuturesOutrightProduct(
        std::string contract_ticker, std::string product_code, schedule::Date trade_date,
        schedule::Date expiry_date, std::optional<schedule::Schedule> payments)
        : contract_ticker_(std::move(contract_ticker)),
          product_code_(std::move(product_code)),
          trade_date_(trade_date),
          expiry_date_(expiry_date),
          payments_(std::move(payments)) {}

std::string_view CommodityFuturesOutrightProduct::UnderlyingId() const { return contract_ticker_; }

OptionType CommodityFuturesOutrightProduct::OptionKind() const { return OptionType::kCall; }

ExerciseStyle CommodityFuturesOutrightProduct::Exercise() const { return ExerciseStyle::kEuropean; }

double CommodityFuturesOutrightProduct::Strike() const { return 0.0; }

const schedule::Date& CommodityFuturesOutrightProduct::TradeDate() const { return trade_date_; }

const schedule::Date& CommodityFuturesOutrightProduct::ExpiryDate() const { return expiry_date_; }

const schedule::Schedule* CommodityFuturesOutrightProduct::PaymentSchedule() const {
    return payments_ ? &(*payments_) : nullptr;
}

std::string_view CommodityFuturesOutrightProduct::ProductCode() const { return product_code_; }

}  // namespace numeraire::products
