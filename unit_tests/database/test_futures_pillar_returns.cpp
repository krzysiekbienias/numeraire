#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <numeraire/database/futures_pillar_returns.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using numeraire::database::FuturesPillarFactorId;
using numeraire::database::FuturesPillarSeries;
using numeraire::database::LoadFuturesPillarCurve;
using numeraire::database::LoadFuturesPillarReturns;
using numeraire::database::LoadLatestFuturesPillarCurve;

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    using namespace std::chrono;
    const auto ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return fs::temp_directory_path() / ("numeraire_fut_pillar_ut_" + std::to_string(ns) + ".sqlite3");
}

void InsertContract(SQLite::Database& db,
                    const std::string& ticker,
                    const std::string& product_code,
                    const std::string& settlement_date) {
    SQLite::Statement ins(db,
                          "INSERT INTO futures_contract (ticker, listing_as_of, product_code, settlement_date, "
                          "source, ingested_at) VALUES (?,?,?,?,'ut','2026-01-01T00:00:00Z')");
    ins.bind(1, ticker);
    ins.bind(2, "2025-01-01");
    ins.bind(3, product_code);
    ins.bind(4, settlement_date);
    ins.exec();
}

void InsertSettle(SQLite::Database& db, const std::string& ticker, const std::string& as_of, double settle) {
    SQLite::Statement ins(db,
                          "INSERT INTO futures_daily_eod (ticker, as_of, open, high, low, close, "
                          "settlement_price, source, ingested_at) VALUES (?,?,?,?,?,?,?,'ut',"
                          "'2026-01-01T00:00:00Z')");
    ins.bind(1, ticker);
    ins.bind(2, as_of);
    ins.bind(3, settle);
    ins.bind(4, settle);
    ins.bind(5, settle);
    ins.bind(6, settle);
    ins.bind(7, settle);
    ins.exec();
}

[[nodiscard]] std::string Session(const int day) {
    std::ostringstream oss;
    oss << "2025-01-" << (day < 10 ? "0" : "") << day;
    return oss.str();
}

/// Three CL contracts in steady contango; the front one settles on 2025-01-10, so
/// M1 rolls from CLF5 to CLG5 on that session.
void SeedContangoCurve(SQLite::Database& db) {
    InsertContract(db, "CLF5", "CL", "2025-01-10");
    InsertContract(db, "CLG5", "CL", "2025-02-10");
    InsertContract(db, "CLH5", "CL", "2025-03-10");

    for (int day = 2; day <= 15; ++day) {
        const std::string as_of = Session(day);
        if (day < 10) {
            InsertSettle(db, "CLF5", as_of, 100.0);
        }
        InsertSettle(db, "CLG5", as_of, 110.0);
        InsertSettle(db, "CLH5", as_of, 120.0);
    }
}

[[nodiscard]] const FuturesPillarSeries& FindPillar(const std::vector<FuturesPillarSeries>& all, const int pillar) {
    for (const FuturesPillarSeries& series : all) {
        if (series.pillar == pillar) {
            return series;
        }
    }
    throw std::runtime_error("pillar not found: M" + std::to_string(pillar));
}

}  // namespace

TEST(FuturesPillarFactorIdTest, SpellsProductAndPillar) {
    EXPECT_EQ(FuturesPillarFactorId("CL", 1), "CL_M1");
    EXPECT_EQ(FuturesPillarFactorId("NG", 12), "NG_M12");
}

// The whole point of rolling: the raw M1 level jumps 100 -> 110 when CLF5 settles,
// but no contract actually moved, so every pillar return must be exactly zero.
TEST(FuturesPillarReturnsTest, RollAcrossContangoProducesNoArtificialReturn) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedContangoCurve(db);
    }

    const auto pillars = LoadFuturesPillarReturns(path.string(), "CL", "2025-01-01", "2025-01-31", 3);
    const FuturesPillarSeries& m1 = FindPillar(pillars, 1);

    EXPECT_EQ(m1.factor_id, "CL_M1");
    ASSERT_GE(m1.returns.size(), 12U);
    for (const auto& r : m1.returns) {
        EXPECT_NEAR(r.log_return, 0.0, 1.0e-12) << "artificial move at " << r.as_of;
    }

    // The roll itself must still produce a return entry, just a zero one.
    bool has_roll_session = false;
    for (const auto& r : m1.returns) {
        has_roll_session = has_roll_session || r.as_of == "2025-01-10";
    }
    EXPECT_TRUE(has_roll_session);

    // After the roll the pillar tracks CLG5 and anchors on its real settle.
    EXPECT_EQ(m1.ticker_on_last_date, "CLG5");
    EXPECT_EQ(m1.last_date, "2025-01-15");
    EXPECT_DOUBLE_EQ(m1.level_on_last_date, 110.0);

    fs::remove(path);
}

TEST(FuturesPillarReturnsTest, CapturesGenuineContractMoves) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertContract(db, "NGF5", "NG", "2025-02-10");
        InsertContract(db, "NGG5", "NG", "2025-03-10");

        // Front contract compounds +1% per session; second stays flat.
        double front = 3.00;
        for (int day = 2; day <= 6; ++day) {
            InsertSettle(db, "NGF5", Session(day), front);
            InsertSettle(db, "NGG5", Session(day), 3.50);
            front *= std::exp(0.01);
        }
    }

    const auto pillars = LoadFuturesPillarReturns(path.string(), "NG", "2025-01-01", "2025-01-31", 2);

    const FuturesPillarSeries& m1 = FindPillar(pillars, 1);
    ASSERT_EQ(m1.returns.size(), 4U);
    for (const auto& r : m1.returns) {
        EXPECT_NEAR(r.log_return, 0.01, 1.0e-12);
    }

    const FuturesPillarSeries& m2 = FindPillar(pillars, 2);
    for (const auto& r : m2.returns) {
        EXPECT_NEAR(r.log_return, 0.0, 1.0e-12);
    }

    fs::remove(path);
}

// The vendor does not quote every contract every session. When the front contract
// is missing the pillar is taken by the next one, every return stays a
// single-contract move, and the history before the hole is kept.
TEST(FuturesPillarReturnsTest, FrontContractMissingPromotesNextAndKeepsEarlierHistory) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertContract(db, "CLF5", "CL", "2025-06-10");

        // Sessions 02..06 exist for the pillar, but 04 is missing from the feed.
        // A second contract keeps every session alive in the curve.
        InsertContract(db, "CLZ5", "CL", "2025-12-10");
        double px = 50.0;
        for (int day = 2; day <= 6; ++day) {
            InsertSettle(db, "CLZ5", Session(day), 60.0);
            if (day != 4) {
                InsertSettle(db, "CLF5", Session(day), px);
            }
            px *= std::exp(0.02);
        }
    }

    const auto pillars = LoadFuturesPillarReturns(path.string(), "CL", "2025-01-01", "2025-01-31", 1);
    const FuturesPillarSeries& m1 = FindPillar(pillars, 1);

    // 03: CLF5 moves +2%. 04: CLF5 unquoted, so CLZ5 holds the pillar and reports
    // its own (flat) move. 05: CLF5 is back but had no quote on 04, so no return.
    // 06: CLF5 quoted on both sessions again.
    ASSERT_EQ(m1.returns.size(), 3U);
    EXPECT_EQ(m1.returns[0].as_of, "2025-01-03");
    EXPECT_NEAR(m1.returns[0].log_return, 0.02, 1.0e-12);
    EXPECT_EQ(m1.returns[1].as_of, "2025-01-04");
    EXPECT_NEAR(m1.returns[1].log_return, 0.0, 1.0e-12);
    EXPECT_EQ(m1.returns[2].as_of, "2025-01-06");
    EXPECT_NEAR(m1.returns[2].log_return, 0.02, 1.0e-12);

    // Never a two-session move dressed up as one session.
    EXPECT_EQ(m1.num_sessions_present, 5);

    fs::remove(path);
}

TEST(FuturesPillarReturnsTest, OmitsPillarsDeeperThanTheListedCurve) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedContangoCurve(db);
    }

    const auto pillars = LoadFuturesPillarReturns(path.string(), "CL", "2025-01-01", "2025-01-31", 6);
    EXPECT_EQ(pillars.size(), 3U);
    EXPECT_EQ(pillars.back().factor_id, "CL_M3");

    fs::remove(path);
}

TEST(FuturesPillarCurveTest, ExactSessionIsEmptyWhenThatDayHasNoQuotes) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertContract(db, "CLF5", "CL", "2025-06-10");
        InsertSettle(db, "CLF5", "2025-01-02", 70.0);
    }

    EXPECT_TRUE(LoadFuturesPillarCurve(path.string(), "CL", "2025-01-03", 1).empty());

    fs::remove(path);
}

TEST(FuturesPillarCurveTest, LatestOccupancyCarriesForwardToLaterDays) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertContract(db, "CLF5", "CL", "2025-06-10");
        InsertContract(db, "CLG5", "CL", "2025-07-10");
        InsertSettle(db, "CLF5", "2025-01-02", 70.0);
        InsertSettle(db, "CLG5", "2025-01-02", 72.0);
    }

    const auto latest = LoadLatestFuturesPillarCurve(path.string(), "CL", "2025-01-07", 2);
    EXPECT_EQ(latest.as_of, "2025-01-02");
    ASSERT_EQ(latest.occupants.size(), 2U);
    EXPECT_EQ(latest.occupants[0].ticker, "CLF5");
    EXPECT_EQ(latest.occupants[1].ticker, "CLG5");
    EXPECT_EQ(latest.occupants[0].factor_id, "CL_M1");

    const auto none = LoadLatestFuturesPillarCurve(path.string(), "CL", "2025-01-01", 2);
    EXPECT_TRUE(none.occupants.empty());
    EXPECT_TRUE(none.as_of.empty());

    fs::remove(path);
}
