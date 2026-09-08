#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <numeraire/database/futures_pillar_returns.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/gabillon_calibration_builder.hpp>
#include <numeraire/utils/exception.hpp>

#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace {

using numeraire::ValidationError;
using numeraire::schedule::Act365FixedYearFraction;
using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::Date;
using numeraire::schedule::FormatIsoDate;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildGabillonCalibration;
using numeraire::simulation::GabillonCalibrationBuildParams;
using numeraire::simulation::GabillonCalibrationBuildStats;

// The curve and its history are generated from the model itself, so the fit has a known
// answer to find rather than merely something plausible to land near.
constexpr double kMeanReversion = 3.0;
constexpr double kShortVol = 0.70;
constexpr double kLongVol = 0.30;
constexpr double kShortLevel = 2.8;
constexpr double kLongLevel = 3.6;
constexpr double kSeasonalCos = 0.12;
constexpr double kSeasonalSin = -0.05;
constexpr int kNumContracts = 40;
/// Contracts listed before `as_of` so the strip has something to roll into.
constexpr int kNumHistoryMonths = 13;
constexpr int kNumQuotedPillars = 8;
constexpr int kNumSessions = 300;
constexpr const char* kAsOf = "2026-09-30";

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    return fs::temp_directory_path() / ("numeraire_gabillon_ut_" + std::to_string(ns) + ".sqlite3");
}

[[nodiscard]] double YearPhase(const Date& date) {
    return Act365FixedYearFraction(Date{.year = date.year, .month = 1, .day = 1}, date);
}

[[nodiscard]] double SeasonalAt(const double phase) {
    const double angle = 2.0 * std::numbers::pi * phase;
    return (kSeasonalCos * std::cos(angle)) + (kSeasonalSin * std::sin(angle));
}

/// Model log forward for a contract `tenor` years out delivering at `phase`.
[[nodiscard]] double ModelLogForward(const double tenor, const double phase) {
    const double decay = std::exp(-kMeanReversion * tenor);
    return (decay * std::log(kShortLevel)) + ((1.0 - decay) * std::log(kLongLevel)) + SeasonalAt(phase);
}

/// Fixed-seed normals: reproducible run to run, and genuinely unit variance, which a
/// hand-rolled oscillator is not — the recovered vols are only meaningful if the shocks
/// that generated them really had the volatility the test claims.
class FixedSeedNormals {
   public:
    [[nodiscard]] double operator()() { return distribution_(engine_); }

   private:
    std::mt19937_64 engine_{20260930U};
    std::normal_distribution<double> distribution_{0.0, 1.0};
};

void SeedGasBook(SQLite::Database& db) {
    db.exec(
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, currency, "
            "contract_size, day_count, calendar) VALUES "
            "('P_NG', 'COMMODITY', 'NG', '2026-10-28', 'PHYSICAL', 'USD', 10000.0, 'Actual365Fixed', "
            "'UnitedStates');");
    db.exec(
            "INSERT INTO trades (trade_id, portfolio_id, strategy_type, booking_timestamp, trade_date, updated_at, "
            "status) VALUES "
            "('TRD_NG', 'BOOK_GAS', 'COMMODITY_FUTURES', '2026-01-01 10:00:00', '2026-01-02', '2026-01-02', "
            "'LIVE');");
    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, execution_price, "
            "commission) VALUES ('L_NG', 'TRD_NG', 'P_NG', 'LONG', 1, 3.0, 0);");
}

/// Where the two factors ended up, which shifts the levels the curve fit should recover:
/// \(\ln F = e^{-k\tau}(\ln S + X) + (1-e^{-k\tau})(\ln L + Y)\).
struct FactorStates {
    double short_state{0.0};
    double long_state{0.0};
};

/// A rolling monthly strip: contracts settle throughout the history, so a pillar keeps
/// roughly the same maturity instead of being one contract growing old. Getting this wrong
/// flattens the observed term structure and hands the long factor volatility that belongs
/// to the short one.
[[nodiscard]] FactorStates SeedGasCurveAndHistory(SQLite::Database& db) {
    const Date as_of = ParseIsoDate(kAsOf);

    std::vector<Date> settlements;
    std::vector<std::string> tickers;
    for (int m = -kNumHistoryMonths; m <= kNumContracts; ++m) {
        const int absolute_month = (as_of.year * 12) + (as_of.month - 1) + m;
        Date settle{};
        settle.year = absolute_month / 12;
        settle.month = (absolute_month % 12) + 1;
        settle.day = 28;
        settlements.push_back(settle);
        tickers.push_back("NG_" + FormatIsoDate(settle));
    }

    SQLite::Statement contract(db,
                               "INSERT INTO futures_contract (ticker, listing_as_of, product_code, "
                               "settlement_date, source, ingested_at) VALUES (?, '2026-01-01', 'NG', ?, 'ut', "
                               "'2026-01-01T00:00:00Z')");
    for (std::size_t i = 0; i < tickers.size(); ++i) {
        contract.reset();
        contract.bind(1, tickers[i]);
        contract.bind(2, FormatIsoDate(settlements[i]));
        contract.exec();
    }

    SQLite::Statement settle_row(db,
                                 "INSERT INTO futures_daily_eod (ticker, as_of, open, high, low, close, "
                                 "settlement_price, source, ingested_at) VALUES (?,?,?,?,?,?,?,'ut',"
                                 "'2026-01-01T00:00:00Z')");
    const auto insert = [&settle_row](const std::string& ticker, const std::string& date, const double price) {
        settle_row.reset();
        settle_row.bind(1, ticker);
        settle_row.bind(2, date);
        settle_row.bind(3, price);
        settle_row.bind(4, price);
        settle_row.bind(5, price);
        settle_row.bind(6, price);
        settle_row.bind(7, price);
        settle_row.exec();
    };

    // Both factors drive every quoted contract, each loaded by its own maturity, so the
    // pillar volatilities the calibrator measures are the model's by construction.
    const double dt = 1.0 / 252.0;
    FixedSeedNormals draw;
    double short_state = 0.0;
    double long_state = 0.0;
    const auto quote_session = [&](const Date& session, const int max_contracts) {
        int quoted = 0;
        for (std::size_t i = 0; i < settlements.size() && quoted < max_contracts; ++i) {
            const double tenor = Act365FixedYearFraction(session, settlements[i]);
            if (tenor <= 0.0) {
                continue;
            }
            const double decay = std::exp(-kMeanReversion * tenor);
            const double log_price = ModelLogForward(tenor, YearPhase(settlements[i])) + (decay * short_state) +
                                     ((1.0 - decay) * long_state);
            insert(tickers[i], FormatIsoDate(session), std::exp(log_price));
            ++quoted;
        }
    };

    // `as_of` is simply the last session, quoted out to the full strip. Restarting the
    // factors for it instead would put the whole accumulated path into one day's return
    // and inflate every pillar's volatility.
    for (int s = kNumSessions; s >= 0; --s) {
        short_state += kShortVol * std::sqrt(dt) * draw();
        long_state += kLongVol * std::sqrt(dt) * draw();
        quote_session(AddCalendarDays(as_of, -s), s == 0 ? kNumContracts : kNumQuotedPillars);
    }
    return FactorStates{.short_state = short_state, .long_state = long_state};
}

[[nodiscard]] GabillonCalibrationBuildParams ParamsFor(const fs::path& db_path) {
    GabillonCalibrationBuildParams params{};
    params.database_file_path = db_path.string();
    params.scope_key = "BOOK_GAS";
    params.as_of = kAsOf;
    params.batch_run_id = "ut";
    params.lookback_calendar_days = 400;
    params.min_return_observations = 30;
    params.commodity_pillars = 6;
    params.seasonal_harmonics = 2;
    return params;
}

class GabillonCalibrationBuilderTest : public ::testing::Test {
   protected:
    void SetUp() override {
        db_path_ = UniqueSqlitePath();
        SQLite::Database db(db_path_.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedGasBook(db);
        states_ = SeedGasCurveAndHistory(db);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(db_path_, ec);
    }

    fs::path db_path_;
    FactorStates states_;
};

[[nodiscard]] double ParamValue(const fs::path& db_path, const std::int64_t calibration_id,
                                const std::string& name) {
    SQLite::Database db(db_path.string(), SQLite::OPEN_READONLY);
    SQLite::Statement st(db,
                         "SELECT param_value FROM calibration_param WHERE calibration_id = ? AND factor_id = 'NG' "
                         "AND param_name = ?");
    st.bind(1, calibration_id);
    st.bind(2, name);
    EXPECT_TRUE(st.executeStep()) << "missing param " << name;
    return st.getColumn(0).getDouble();
}

}  // namespace

TEST_F(GabillonCalibrationBuilderTest, RecoversTheSeasonalShapeFromTheForwardCurve) {
    const GabillonCalibrationBuildStats stats = BuildGabillonCalibration(ParamsFor(db_path_));

    ASSERT_EQ(stats.curves.size(), 1U);
    EXPECT_EQ(stats.curves.front().product_code, "NG");
    EXPECT_EQ(stats.curves.front().num_curve_contracts, static_cast<std::size_t>(kNumContracts));
    EXPECT_LT(stats.curves.front().curve_log_rmse, 1.0e-6);

    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "seasonal_cos_1"), kSeasonalCos, 1.0e-4);
    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "seasonal_sin_1"), kSeasonalSin, 1.0e-4);
    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "seasonal_cos_2"), 0.0, 1.0e-4);
    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "seasonal_sin_2"), 0.0, 1.0e-4);
    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "curve_mean_reversion"), kMeanReversion, 1.0e-2);
    // The curve is observed after the factors have wandered, so the levels it reveals are
    // the starting ones carried by the state they reached.
    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "curve_short_level"),
                kShortLevel * std::exp(states_.short_state), 1.0e-3);
    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "curve_long_level"),
                kLongLevel * std::exp(states_.long_state), 1.0e-3);
}

TEST_F(GabillonCalibrationBuilderTest, RecoversDynamicsFromThePillarVolatilityStructure) {
    const GabillonCalibrationBuildStats stats = BuildGabillonCalibration(ParamsFor(db_path_));

    ASSERT_EQ(stats.curves.size(), 1U);
    const auto& curve = stats.curves.front();
    // Sampling noise over 120 sessions is real, so this checks the structure rather than
    // exact numbers: the front is driven harder than the back, and the handover is quick.
    EXPECT_GT(curve.short_factor_vol, curve.long_factor_vol);
    EXPECT_NEAR(curve.short_factor_vol, kShortVol, 0.25 * kShortVol);
    EXPECT_NEAR(curve.long_factor_vol, kLongVol, 0.35 * kLongVol);
    EXPECT_GT(curve.mean_reversion, 0.5);
    EXPECT_LT(curve.vol_rmse, 0.02);
    EXPECT_EQ(curve.num_vol_pillars, 6U);
    EXPECT_NEAR(ParamValue(db_path_, stats.calibration_id, "vol_fit_pillars"), 6.0, 1.0e-9);
}

TEST_F(GabillonCalibrationBuilderTest, WritesTwoStateFactorsCarryingNoPriceLevel) {
    const GabillonCalibrationBuildStats stats = BuildGabillonCalibration(ParamsFor(db_path_));
    EXPECT_EQ(stats.num_factors, 2);

    SQLite::Database db(db_path_.string(), SQLite::OPEN_READONLY);
    SQLite::Statement st(db,
                         "SELECT factor_id, factor_level IS NULL, volatility FROM calibration_factor "
                         "WHERE calibration_id = ? ORDER BY factor_index");
    st.bind(1, stats.calibration_id);

    ASSERT_TRUE(st.executeStep());
    EXPECT_EQ(st.getColumn(0).getString(), "NG_SHORT");
    // A state variable has no observable price: the simulation anchors dated contracts on
    // their own settles, so a fitted level here would only invite someone to use it.
    EXPECT_EQ(st.getColumn(1).getInt(), 1);
    EXPECT_GT(st.getColumn(2).getDouble(), 0.0);

    ASSERT_TRUE(st.executeStep());
    EXPECT_EQ(st.getColumn(0).getString(), "NG_LONG");
    EXPECT_EQ(st.getColumn(1).getInt(), 1);
    EXPECT_FALSE(st.executeStep());
}

TEST_F(GabillonCalibrationBuilderTest, StoresSnapshotAsFittedGabillonNotHistoricalGbm) {
    const GabillonCalibrationBuildStats stats = BuildGabillonCalibration(ParamsFor(db_path_));

    SQLite::Database db(db_path_.string(), SQLite::OPEN_READONLY);
    SQLite::Statement st(db,
                         "SELECT model, source, scope_key, as_of FROM calibration_snapshot WHERE calibration_id = ?");
    st.bind(1, stats.calibration_id);
    ASSERT_TRUE(st.executeStep());
    EXPECT_EQ(st.getColumn(0).getString(), "gabillon_2f");
    EXPECT_EQ(st.getColumn(1).getString(), "fit");
    EXPECT_EQ(st.getColumn(2).getString(), "BOOK_GAS");
    EXPECT_EQ(st.getColumn(3).getString(), kAsOf);
}

TEST_F(GabillonCalibrationBuilderTest, RejectsABookWithNoCommodityLegs) {
    GabillonCalibrationBuildParams params = ParamsFor(db_path_);
    params.scope_key = "BOOK_WITHOUT_COMMODITIES";
    EXPECT_THROW(std::ignore = BuildGabillonCalibration(params), ValidationError);
}
