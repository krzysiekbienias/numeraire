#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/historical_calibrator.hpp>
#include <numeraire/utils/exception.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace {

using numeraire::database::DailyCloseObservation;
using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::FormatIsoDate;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::CalibrateFromPriceHistory;
using numeraire::simulation::HistoricalCalibratorConfig;

[[nodiscard]] std::vector<DailyCloseObservation> BuildCorrelatedSeries(const numeraire::schedule::Date& start,
                                                                       const int num_days,
                                                                       const double start_price,
                                                                       const double daily_shock_scale) {
    std::vector<DailyCloseObservation> out;
    out.reserve(static_cast<std::size_t>(num_days));
    numeraire::schedule::Date date = start;
    double price = start_price;
    for (int day = 0; day < num_days; ++day) {
        out.push_back(DailyCloseObservation{.as_of = FormatIsoDate(date), .close = price});
        const double shock = daily_shock_scale * std::sin(0.17 * static_cast<double>(day));
        price *= std::exp(shock);
        date = AddCalendarDays(date, 1);
    }
    return out;
}

TEST(HistoricalCalibratorTest, IdenticalReturnStructureGivesHighCorrelation) {
    const auto start = ParseIsoDate("2024-01-02");
    const int num_days = 120;
    std::unordered_map<std::string, std::vector<DailyCloseObservation>> closes_by_factor;
    closes_by_factor.emplace("AAPL", BuildCorrelatedSeries(start, num_days, 100.0, 0.012));
    closes_by_factor.emplace("MSFT", BuildCorrelatedSeries(start, num_days, 200.0, 0.012));

    HistoricalCalibratorConfig config;
    config.as_of = AddCalendarDays(start, num_days - 1);
    config.min_return_observations = 60;

    const auto result = CalibrateFromPriceHistory({"AAPL", "MSFT"}, closes_by_factor, config);
    ASSERT_EQ(result.factor_ids.size(), 2U);
    EXPECT_EQ(result.num_return_observations, static_cast<std::size_t>(num_days - 1));
    EXPECT_NEAR(result.correlation[1], 1.0, 1.0e-6);
    EXPECT_NEAR(result.correlation[2], 1.0, 1.0e-6);
    EXPECT_NEAR(result.correlation[3], 1.0, 1.0e-6);
    EXPECT_NEAR(result.volatilities[0], result.volatilities[1], 1.0e-6);
    EXPECT_GT(result.volatilities[0], 0.0);
    EXPECT_EQ(result.cholesky.n, 2U);
}

TEST(HistoricalCalibratorTest, SingleFactorProducesUnitCorrelationAndCholesky) {
    const auto start = ParseIsoDate("2024-06-01");
    const int num_days = 80;
    std::unordered_map<std::string, std::vector<DailyCloseObservation>> closes_by_factor;
    closes_by_factor.emplace("SPY", BuildCorrelatedSeries(start, num_days, 450.0, 0.008));

    HistoricalCalibratorConfig config;
    config.as_of = AddCalendarDays(start, num_days - 1);
    config.min_return_observations = 30;

    const auto result = CalibrateFromPriceHistory({"SPY"}, closes_by_factor, config);
    ASSERT_EQ(result.factor_ids.size(), 1U);
    EXPECT_DOUBLE_EQ(result.correlation[0], 1.0);
    EXPECT_EQ(result.cholesky.n, 1U);
    EXPECT_DOUBLE_EQ(result.cholesky.lower[0], 1.0);
    EXPECT_GT(result.volatilities[0], 0.0);
}

TEST(HistoricalCalibratorTest, RejectsInsufficientHistory) {
    const auto start = ParseIsoDate("2024-01-02");
    std::unordered_map<std::string, std::vector<DailyCloseObservation>> closes_by_factor;
    closes_by_factor.emplace("AAPL", BuildCorrelatedSeries(start, 10, 100.0, 0.01));

    HistoricalCalibratorConfig config;
    config.as_of = AddCalendarDays(start, 9);
    config.min_return_observations = 60;

    EXPECT_THROW(CalibrateFromPriceHistory({"AAPL"}, closes_by_factor, config), numeraire::ValidationError);
}

}  // namespace
