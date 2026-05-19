#include <numeraire/core/imarket_data.hpp>
#include <numeraire/market_data/market_snapshot.hpp>
#include <numeraire/market_data/static_market_data_provider.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>
#include <utility>

#include <numeraire/schedule/date.hpp>

namespace numeraire::market_data {
namespace {

class SnapshotBackedMarketData final : public core::IMarketData {
public:
    explicit SnapshotBackedMarketData(MarketSnapshot snapshot) : snapshot_(std::move(snapshot)) {}

    [[nodiscard]] const schedule::Date& ValuationDate() const override { return snapshot_.valuation_date; }

    [[nodiscard]] double Spot(const std::string_view underlying_id) const override {
        const std::string key(underlying_id);
        const auto it = snapshot_.spots.find(key);
        if (it == snapshot_.spots.end()) {
            throw MarketDataError("Spot: unknown underlying \"" + key + "\"");
        }
        return it->second;
    }

    [[nodiscard]] double RiskFreeRate() const override { return snapshot_.risk_free_rate; }

    [[nodiscard]] double DividendYield(const std::string_view underlying_id) const override {
        const std::string key(underlying_id);
        const auto it = snapshot_.dividend_yields.find(key);
        if (it == snapshot_.dividend_yields.end()) {
            return 0.0;
        }
        return it->second;
    }

    [[nodiscard]] double ImpliedVolatility(const std::string_view underlying_id,
                                           const double strike,
                                           const double time_to_expiry_years) const override {
        static_cast<void>(underlying_id);
        static_cast<void>(strike);
        static_cast<void>(time_to_expiry_years);
        return snapshot_.flat_implied_volatility;
    }

private:
    MarketSnapshot snapshot_;
};

}  // namespace

StaticMarketDataProvider::StaticMarketDataProvider(MarketSnapshot snapshot) : snapshot_(std::move(snapshot)) {}

std::unique_ptr<core::IMarketData> StaticMarketDataProvider::CreateMarketData() const {
    return std::make_unique<SnapshotBackedMarketData>(snapshot_);
}

}  // namespace numeraire::market_data
