#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/schedule/date.hpp>

#include <string>
#include <unordered_map>

namespace {

class MapBackedMarketData final : public numeraire::core::IMarketData {
   public:
    MapBackedMarketData() = default;

    double risk_free_rate{0.03};

    void SetSpot(std::string ticker, const double value) { spots_[std::move(ticker)] = value; }

    void SetDividendYield(std::string ticker, const double value) {
        divs_[std::move(ticker)] = value;
    }

    void SetVol(const double value) { flat_vol_ = value; }

    void SetValuationDate(const numeraire::schedule::Date& date) { valuation_date_ = date; }

    [[nodiscard]] const numeraire::schedule::Date& ValuationDate() const override {
        return valuation_date_;
    }

    [[nodiscard]] double Spot(const std::string_view underlying_id) const override {
        return spots_.at(std::string(underlying_id));
    }

    [[nodiscard]] double RiskFreeRate() const override { return risk_free_rate; }

    [[nodiscard]] double DividendYield(const std::string_view underlying_id) const override {
        const auto it = divs_.find(std::string(underlying_id));
        if (it == divs_.end()) {
            return 0.0;
        }
        return it->second;
    }

    [[nodiscard]] double ImpliedVolatility(const std::string_view underlying_id, const double strike,
                                           const double time_to_expiry_years) const override {
        static_cast<void>(underlying_id);
        static_cast<void>(strike);
        static_cast<void>(time_to_expiry_years);
        return flat_vol_;
    }

   private:
    std::unordered_map<std::string, double> spots_;
    std::unordered_map<std::string, double> divs_;
    double flat_vol_{0.25};
    numeraire::schedule::Date valuation_date_{.year = 2025, .month = 6, .day = 1};
};

}  // namespace

TEST(IMarketDataTest, ConcreteImplementationReturnsConfiguredValues) {
    MapBackedMarketData m;
    m.SetSpot("AAPL", 180.0);
    m.SetDividendYield("AAPL", 0.005);
    m.risk_free_rate = 0.04;
    m.SetVol(0.22);

    EXPECT_DOUBLE_EQ(m.Spot("AAPL"), 180.0);
    EXPECT_DOUBLE_EQ(m.DividendYield("AAPL"), 0.005);
    EXPECT_DOUBLE_EQ(m.RiskFreeRate(), 0.04);
    EXPECT_DOUBLE_EQ(m.ImpliedVolatility("AAPL", 170.0, 0.25), 0.22);
}
