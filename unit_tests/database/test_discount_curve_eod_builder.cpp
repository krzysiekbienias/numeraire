#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <numeraire/database/discount_curve_eod_builder.hpp>
#include <numeraire/quant/discount_curve_bootstrap.hpp>
#include <numeraire/utils/logger.hpp>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string ReadSchemaFile() {
    fs::path const schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    if (!in) {
        throw std::runtime_error("failed to open schema: " + schema.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void SeedParCurve(SQLite::Database& db) {
    SQLite::Statement header(db,
                             "INSERT INTO par_curve_eod (curve_id, as_of, currency, curve_kind, source, day_count, "
                             "session_calendar, ingested_at) VALUES (?,?,?,?,?,?,?,?)");
    header.bind(1, "USD_TREASURY_PAR_FRED");
    header.bind(2, "2026-05-27");
    header.bind(3, "USD");
    header.bind(4, "treasury_par_fred");
    header.bind(5, "FRED");
    header.bind(6, "Actual365Fixed");
    header.bind(7, "America/New_York");
    header.bind(8, "2026-05-28T00:00:00Z");
    header.exec();

    struct Row {
        const char* tenor;
        int tenor_days;
        const char* instrument_type;
        double quoted_rate;
    };

    const Row rows[] = {
            {.tenor = "1M", .tenor_days = 30, .instrument_type = "deposit", .quoted_rate = 0.0411},
            {.tenor = "3M", .tenor_days = 91, .instrument_type = "deposit", .quoted_rate = 0.0410},
            {.tenor = "6M", .tenor_days = 182, .instrument_type = "deposit", .quoted_rate = 0.0379},
            {.tenor = "1Y", .tenor_days = 365, .instrument_type = "deposit", .quoted_rate = 0.0368},
            {.tenor = "2Y", .tenor_days = 730, .instrument_type = "swap", .quoted_rate = 0.0400},
            {.tenor = "3Y", .tenor_days = 1095, .instrument_type = "swap", .quoted_rate = 0.0395},
            {.tenor = "5Y", .tenor_days = 1825, .instrument_type = "swap", .quoted_rate = 0.0415},
            {.tenor = "7Y", .tenor_days = 2555, .instrument_type = "swap", .quoted_rate = 0.0435},
            {.tenor = "10Y", .tenor_days = 3650, .instrument_type = "swap", .quoted_rate = 0.0455},
    };

    SQLite::Statement point(db,
                            "INSERT INTO par_curve_point_eod (curve_id, as_of, tenor, tenor_days, instrument_type, "
                            "fred_series_id, quoted_rate) VALUES (?,?,?,?,?,?,?)");
    for (const Row& row : rows) {
        point.bind(1, "USD_TREASURY_PAR_FRED");
        point.bind(2, "2026-05-27");
        point.bind(3, row.tenor);
        point.bind(4, row.tenor_days);
        point.bind(5, row.instrument_type);
        point.bind(6, "TEST");
        point.bind(7, row.quoted_rate);
        point.exec();
        point.reset();
    }
}

}  // namespace

TEST(DiscountCurveEodBuilderTest, BootstrapsFromParCurveQuotes) {
    numeraire::utils::Logger::Init();
    const fs::path db_path = fs::temp_directory_path() / "numeraire_discount_curve_builder_test.sqlite3";
    std::error_code ec;
    fs::remove(db_path, ec);

    SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON");
    db.exec(ReadSchemaFile());
    SeedParCurve(db);

    std::vector<numeraire::quant::CurvePillarQuote> quotes;
    quotes.push_back({.tenor = "1M",
                      .tenor_days = 30,
                      .kind = numeraire::quant::CurvePillarKind::kDeposit,
                      .quoted_rate = 0.0411});
    quotes.push_back({.tenor = "3M",
                      .tenor_days = 91,
                      .kind = numeraire::quant::CurvePillarKind::kDeposit,
                      .quoted_rate = 0.0410});
    quotes.push_back({.tenor = "6M",
                      .tenor_days = 182,
                      .kind = numeraire::quant::CurvePillarKind::kDeposit,
                      .quoted_rate = 0.0379});
    quotes.push_back({.tenor = "1Y",
                      .tenor_days = 365,
                      .kind = numeraire::quant::CurvePillarKind::kDeposit,
                      .quoted_rate = 0.0368});
    quotes.push_back({.tenor = "2Y",
                      .tenor_days = 730,
                      .kind = numeraire::quant::CurvePillarKind::kSwap,
                      .quoted_rate = 0.0400});
    quotes.push_back({.tenor = "3Y",
                      .tenor_days = 1095,
                      .kind = numeraire::quant::CurvePillarKind::kSwap,
                      .quoted_rate = 0.0395});
    quotes.push_back({.tenor = "5Y",
                      .tenor_days = 1825,
                      .kind = numeraire::quant::CurvePillarKind::kSwap,
                      .quoted_rate = 0.0415});
    quotes.push_back({.tenor = "7Y",
                      .tenor_days = 2555,
                      .kind = numeraire::quant::CurvePillarKind::kSwap,
                      .quoted_rate = 0.0435});
    quotes.push_back({.tenor = "10Y",
                      .tenor_days = 3650,
                      .kind = numeraire::quant::CurvePillarKind::kSwap,
                      .quoted_rate = 0.0455});
    const auto expected = numeraire::quant::BootstrapDiscountCurve(quotes);
    ASSERT_EQ(expected.status, numeraire::quant::BootstrapStatus::kOk);

    numeraire::database::DiscountCurveBuildParams params{};
    params.database_file_path = db_path.string();
    params.curve_id = "USD_TREASURY_PAR_FRED";
    params.as_of = "2026-05-27";
    params.batch_run_id = "unit-test";

    const auto stats = numeraire::database::BuildDiscountCurveEod(params);
    EXPECT_EQ(stats.pillars_loaded, 9);
    EXPECT_EQ(stats.pillars_written, 9);

    SQLite::Database ro(db_path.string(), SQLite::OPEN_READONLY);
    SQLite::Statement q(ro,
                        "SELECT p.tenor, p.time_years, p.zero_rate, p.discount_factor "
                        "FROM discount_curve_point_eod p "
                        "INNER JOIN discount_curve_eod h ON h.curve_id = p.curve_id AND h.as_of = p.as_of "
                        "WHERE h.curve_id = 'USD_TREASURY_PAR_FRED' AND h.as_of = '2026-05-27' "
                        "ORDER BY p.time_years");
    std::size_t i = 0;
    while (q.executeStep()) {
        ASSERT_LT(i, expected.pillars.size()) << "unexpected extra pillar row";
        EXPECT_EQ(q.getColumn(0).getString(), expected.pillars[i].tenor);
        EXPECT_NEAR(q.getColumn(1).getDouble(), expected.pillars[i].time_years, 1.0e-12);
        EXPECT_NEAR(q.getColumn(2).getDouble(), expected.pillars[i].zero_rate, 1.0e-12);
        EXPECT_NEAR(q.getColumn(3).getDouble(), expected.pillars[i].discount_factor, 1.0e-12);
        ++i;
    }
    EXPECT_EQ(i, expected.pillars.size());
}
