#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <numeraire/database/discount_curve_eod_builder.hpp>
#include <numeraire/database/par_curve_quote_loader.hpp>
#include <numeraire/database/sqlite_discount_curve_repository.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/quant/discount_curve_bootstrap.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace numeraire::database {
namespace {

using numeraire::quant::BootstrapDiscountCurve;
using numeraire::quant::BootstrapStatus;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

[[nodiscard]] std::string IsoUtcNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&t, &utc);
    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

[[nodiscard]] bool LooksIsoDate(const std::string& s) {
    return s.size() == 10U && s[4] == '-' && s[7] == '-';
}

[[nodiscard]] std::string FormatIsoDate(const schedule::Date& d) {
    std::ostringstream oss;
    oss << std::setfill('0') << d.year << '-' << std::setw(2) << d.month << '-' << std::setw(2) << d.day;
    return oss.str();
}

[[nodiscard]] int CompareIsoDate(const std::string& a, const std::string& b) {
    if (a == b) {
        return 0;
    }
    return a < b ? -1 : 1;
}

[[nodiscard]] const char* BootstrapStatusLabel(const BootstrapStatus status) {
    switch (status) {
    case BootstrapStatus::kOk:
        return "ok";
    case BootstrapStatus::kInvalidInputs:
        return "invalid_inputs";
    case BootstrapStatus::kUnsupportedInstrument:
        return "unsupported_instrument";
    case BootstrapStatus::kNoConvergence:
        return "no_convergence";
    }
    return "unknown";
}

[[nodiscard]] DiscountCurveBuildStats BuildOneDay(const DiscountCurveBuildParams& params) {
    DiscountCurveBuildStats stats{};

    const std::optional<ParCurveEodHeader> par_header =
            LoadParCurveEodHeader(params.database_file_path, params.curve_id, params.as_of);
    if (!par_header.has_value()) {
        throw ValidationError("No par_curve_eod for curve_id=" + params.curve_id + " as_of=" + params.as_of);
    }

    const std::vector<ParCurvePillarInput> pillar_inputs =
            LoadParCurvePillarInputs(params.database_file_path, params.curve_id, params.as_of);
    stats.pillars_loaded = static_cast<int>(pillar_inputs.size());
    if (pillar_inputs.empty()) {
        throw ValidationError("No par_curve_point_eod rows for curve_id=" + params.curve_id + " as_of=" + params.as_of);
    }

    const std::vector<quant::CurvePillarQuote> quotes = ToCurvePillarQuotes(pillar_inputs);
    const quant::BootstrapResult bootstrap = BootstrapDiscountCurve(quotes);
    if (bootstrap.status != BootstrapStatus::kOk) {
        throw ValidationError(std::string{"BootstrapDiscountCurve failed: "} + BootstrapStatusLabel(bootstrap.status));
    }
    if (bootstrap.pillars.size() != quotes.size()) {
        throw ValidationError("BootstrapDiscountCurve returned unexpected pillar count");
    }

    DiscountCurveEodHeaderWrite header{};
    header.curve_id = params.curve_id;
    header.as_of = params.as_of;
    header.source_par_curve_id = par_header->curve_id;
    header.source_par_as_of = par_header->as_of;
    header.currency = par_header->currency;
    header.day_count = par_header->day_count;
    header.session_calendar = par_header->session_calendar;
    header.batch_run_id = params.batch_run_id;
    header.ingested_at = IsoUtcNow();

    std::vector<DiscountCurvePointWrite> points;
    points.reserve(bootstrap.pillars.size());
    for (const quant::BootstrappedCurvePoint& pillar : bootstrap.pillars) {
        points.push_back(DiscountCurvePointWrite{
                .tenor = pillar.tenor,
                .time_years = pillar.time_years,
                .zero_rate = pillar.zero_rate,
                .discount_factor = pillar.discount_factor,
        });
    }

    SqliteDiscountCurveRepository repo(params.database_file_path);
    repo.UpsertCurve(header, points);
    stats.pillars_written = static_cast<int>(points.size());
    return stats;
}

}  // namespace

DiscountCurveBuildStats BuildDiscountCurveEod(const DiscountCurveBuildParams& params) {
    return BuildOneDay(params);
}

void PrintDiscountCurveEodBuildUsageLines() {
    Logger::NumError(
            "  dev_main --build-discount-curve-eod --as-of YYYY-MM-DD "
            "[--curve-id USD_TREASURY_PAR_FRED] [--from YYYY-MM-DD --to YYYY-MM-DD]\n"
            "    Bootstrap discount factors from `par_curve_*` quotes (deposit/swap solver + "
            "linear zero-rate interpolation).\n"
            "    Writes `discount_curve_eod` + `discount_curve_point_eod`.");
}

int TryRunDiscountCurveEodBuild(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string as_of;
    std::string from_iso;
    std::string to_iso;
    std::string curve_id = "USD_TREASURY_PAR_FRED";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--build-discount-curve-eod") == 0) {
            mode = true;
        } else if (std::strcmp(argv[i], "--as-of") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--as-of requires YYYY-MM-DD.");
                return 1;
            }
            as_of = argv[++i];
        } else if (std::strcmp(argv[i], "--from") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--from requires YYYY-MM-DD.");
                return 1;
            }
            from_iso = argv[++i];
        } else if (std::strcmp(argv[i], "--to") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--to requires YYYY-MM-DD.");
                return 1;
            }
            to_iso = argv[++i];
        } else if (std::strcmp(argv[i], "--curve-id") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--curve-id requires a curve identifier.");
                return 1;
            }
            curve_id = argv[++i];
        }
    }

    if (!mode) {
        return -1;
    }

    if (!from_iso.empty() || !to_iso.empty()) {
        if (from_iso.empty() || to_iso.empty()) {
            Logger::NumError("--from and --to must be used together.");
            return 1;
        }
        if (!LooksIsoDate(from_iso) || !LooksIsoDate(to_iso)) {
            Logger::NumError("--from/--to must be YYYY-MM-DD.");
            return 1;
        }
        if (CompareIsoDate(from_iso, to_iso) > 0) {
            Logger::NumError("--from must be <= --to.");
            return 1;
        }
    } else if (!as_of.empty()) {
        if (!LooksIsoDate(as_of)) {
            Logger::NumError("--as-of must be YYYY-MM-DD.");
            return 1;
        }
        from_iso = as_of;
        to_iso = as_of;
    } else {
        Logger::NumError("Provide --as-of YYYY-MM-DD or --from/--to range.");
        PrintDiscountCurveEodBuildUsageLines();
        return 1;
    }

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    DiscountCurveBuildParams params{};
    params.database_file_path = db_path.string();
    params.curve_id = curve_id;
    params.batch_run_id = "discount-curve-" + from_iso + "-" + to_iso;

    Logger::NumInfo("build-discount-curve-eod → SQLite {} curve_id={} range {}..{}.",
                    db_path.string(),
                    curve_id,
                    from_iso,
                    to_iso);

    schedule::Date cursor = schedule::ParseIsoDate(from_iso);
    const schedule::Date end = schedule::ParseIsoDate(to_iso);
    int days_ok = 0;
    for (;;) {
        params.as_of = FormatIsoDate(cursor);
        try {
            const DiscountCurveBuildStats stats = BuildDiscountCurveEod(params);
            Logger::NumInfo("  {}: bootstrapped {} pillar(s).", params.as_of, stats.pillars_written);
            ++days_ok;
        } catch (const ValidationError& ex) {
            Logger::NumWarn("Skipping {}: {}.", params.as_of, ex.what());
        }
        if (cursor.year == end.year && cursor.month == end.month && cursor.day == end.day) {
            break;
        }
        cursor = schedule::FromQuantLibDate(schedule::ToQuantLibDate(cursor) + 1);
    }

    Logger::NumInfo("build-discount-curve-eod finished: {} day(s) built in range {}..{}.", days_ok, from_iso, to_iso);
    return 0;
}

}  // namespace numeraire::database
