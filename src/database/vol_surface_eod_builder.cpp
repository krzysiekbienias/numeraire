#include <numeraire/database/vol_surface_eod_builder.hpp>

#include <numeraire/database/index_daily_eod_lookup.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/database/sqlite_vol_surface_repository.hpp>
#include <numeraire/database/vol_surface_quote_loader.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/quant/implied_vol_european.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>
#include <numeraire/utils/string.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace numeraire::database {
namespace {

using numeraire::OptionType;
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

[[nodiscard]] bool IsEuropeanExercise(const std::string& exercise_style) {
    const std::string key = utils::ToLowerAscii(utils::TrimCopy(exercise_style));
    return key == "european" || key.empty();
}

[[nodiscard]] const char* ContractTypeLabel(const OptionType kind) {
    return kind == OptionType::kCall ? "call" : "put";
}

[[nodiscard]] std::string SliceKey(const std::string& expiration_date, const double strike,
                                   const std::string& contract_type) {
    return expiration_date + '|' + std::to_string(strike) + '|' + contract_type;
}

[[nodiscard]] const char* QualityFromStatus(const quant::ImpliedVolStatus status) {
    switch (status) {
        case quant::ImpliedVolStatus::kOk:
            return "ok";
        case quant::ImpliedVolStatus::kInvalidInputs:
            return "invalid_inputs";
        case quant::ImpliedVolStatus::kBelowIntrinsic:
            return "below_intrinsic";
        case quant::ImpliedVolStatus::kNoConvergence:
            return "no_convergence";
    }
    return "unknown";
}

[[nodiscard]] double EnvDouble(const char* key, const double default_value) noexcept {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const double v = std::strtod(raw, &end);
    if (end == raw) {
        return default_value;
    }
    return v;
}

[[nodiscard]] VolSurfaceBuildStats BuildOneDay(const VolSurfaceBuildParams& params) {
    VolSurfaceBuildStats stats{};

    const std::optional<double> spot =
            LookupIndexDailyClose(params.database_file_path, params.index_ticker, params.as_of, params.adjusted);
    if (!spot.has_value()) {
        throw ValidationError("No index_daily_eod close for ticker=" + params.index_ticker + " as_of=" + params.as_of);
    }

    const std::vector<VolSurfaceOptionQuoteInput> quotes =
            LoadVolSurfaceOptionQuotes(params.database_file_path, params.underlying_id, params.as_of, params.adjusted);
    stats.quotes_loaded = static_cast<int>(quotes.size());

    const schedule::Date valuation = schedule::ParseIsoDate(params.as_of);
    std::vector<VolSurfacePointWrite> points;
    points.reserve(quotes.size());
    std::unordered_set<std::string> seen_slices;

    for (const VolSurfaceOptionQuoteInput& q : quotes) {
        if (!IsEuropeanExercise(q.exercise_style)) {
            ++stats.skipped_non_european;
            continue;
        }
        if (!LooksIsoDate(q.expiration_date)) {
            ++stats.skipped_bad_catalog;
            continue;
        }

        const schedule::Date expiry = schedule::ParseIsoDate(q.expiration_date);
        const double tau = schedule::Act365FixedYearFraction(valuation, expiry);
        const quant::ImpliedVolResult iv = quant::ImpliedVolEuropeanVanilla(
                q.option_type, q.close, *spot, q.strike, params.risk_free_rate, params.dividend_yield, tau);

        if (iv.status == quant::ImpliedVolStatus::kOk) {
            const std::string contract_type = ContractTypeLabel(q.option_type);
            const std::string slice = SliceKey(q.expiration_date, q.strike, contract_type);
            if (!seen_slices.insert(slice).second) {
                ++stats.skipped_duplicate_slice;
                continue;
            }
            VolSurfacePointWrite pt{};
            pt.expiration_date = q.expiration_date;
            pt.years_to_maturity = tau;
            pt.strike = q.strike;
            pt.contract_type = contract_type;
            pt.implied_vol = iv.implied_vol;
            pt.source_option_ticker = q.option_ticker;
            pt.input_price = q.close;
            pt.quality = QualityFromStatus(iv.status);
            points.push_back(std::move(pt));
            ++stats.points_written;
            continue;
        }

        switch (iv.status) {
            case quant::ImpliedVolStatus::kInvalidInputs:
                ++stats.skipped_invalid_inputs;
                break;
            case quant::ImpliedVolStatus::kBelowIntrinsic:
                ++stats.skipped_below_intrinsic;
                break;
            case quant::ImpliedVolStatus::kNoConvergence:
                ++stats.skipped_no_convergence;
                break;
            case quant::ImpliedVolStatus::kOk:
                break;
        }
    }

    VolSurfaceEodHeaderWrite header{};
    header.underlying_id = params.underlying_id;
    header.as_of = params.as_of;
    header.surface_kind = params.surface_kind;
    header.spot_used = *spot;
    header.risk_free_rate = params.risk_free_rate;
    header.dividend_yield = params.dividend_yield;
    header.ingested_at = IsoUtcNow();
    header.batch_run_id = params.batch_run_id;

    SqliteVolSurfaceRepository repo(params.database_file_path);
    static_cast<void>(repo.UpsertSurface(header, points));

    Logger::NumInfo(
            "vol surface {} as_of={} spot={} quotes={} points={} "
            "skip(inv/below/conv/non-eu/bad/dup)={}/{}/{}/{}/{}/{}.",
            params.underlying_id,
            params.as_of,
            *spot,
            stats.quotes_loaded,
            stats.points_written,
            stats.skipped_invalid_inputs,
            stats.skipped_below_intrinsic,
            stats.skipped_no_convergence,
            stats.skipped_non_european,
            stats.skipped_bad_catalog,
            stats.skipped_duplicate_slice);

    return stats;
}

}  // namespace

VolSurfaceBuildStats BuildVolSurfaceEod(const VolSurfaceBuildParams& params) {
    return BuildOneDay(params);
}

void PrintVolSurfaceEodBuildUsageLines() {
    Logger::NumError(
            "  dev_main --build-vol-surface-eod --as-of YYYY-MM-DD --underlying NDX "
            "[--index-ticker I:NDX] [--from YYYY-MM-DD --to YYYY-MM-DD]\n"
            "    Invert European BS implied vol from `option_daily_price_eod` + `index_daily_eod` close.\n"
            "    Writes `vol_surface_eod` + `vol_surface_point_eod`. Rate/div: NUMERAIRE_DEV_RATE / "
            "NUMERAIRE_DEV_DIV_YIELD (defaults 0.03 / 0). Adjusted flag: NUMERAIRE_DEV_SPOT_ADJUSTED (default 1).");
}

int TryRunVolSurfaceEodBuild(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string as_of;
    std::string from_iso;
    std::string to_iso;
    std::string underlying;
    std::string index_ticker;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--build-vol-surface-eod") == 0) {
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
        } else if (std::strcmp(argv[i], "--underlying") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--underlying requires a symbol (e.g. NDX).");
                return 1;
            }
            underlying = argv[++i];
        } else if (std::strcmp(argv[i], "--index-ticker") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--index-ticker requires a symbol.");
                return 1;
            }
            index_ticker = argv[++i];
        }
    }

    if (!mode) {
        return -1;
    }

    if (underlying.empty()) {
        Logger::NumError("--build-vol-surface-eod requires --underlying.");
        PrintVolSurfaceEodBuildUsageLines();
        return 1;
    }

    if (index_ticker.empty()) {
        if (underlying == "NDX") {
            index_ticker = "I:NDX";
        } else {
            Logger::NumError("--index-ticker is required unless --underlying is NDX (default I:NDX).");
            return 1;
        }
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
        PrintVolSurfaceEodBuildUsageLines();
        return 1;
    }

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    const int adjusted = EnvDouble("NUMERAIRE_DEV_SPOT_ADJUSTED", 1.0) >= 0.5 ? 1 : 0;
    const double rate = EnvDouble("NUMERAIRE_DEV_RATE", 0.03);
    const double div = EnvDouble("NUMERAIRE_DEV_DIV_YIELD", 0.0);

    VolSurfaceBuildParams params{};
    params.database_file_path = db_path.string();
    params.underlying_id = underlying;
    params.index_ticker = index_ticker;
    params.risk_free_rate = rate;
    params.dividend_yield = div;
    params.adjusted = adjusted;
    params.batch_run_id = "vol-surface-" + from_iso + "-" + to_iso;

    Logger::NumInfo("build-vol-surface-eod → SQLite {} underlying={} index={} range {}..{}.",
                    db_path.string(),
                    underlying,
                    index_ticker,
                    from_iso,
                    to_iso);

    schedule::Date cursor = schedule::ParseIsoDate(from_iso);
    const schedule::Date end = schedule::ParseIsoDate(to_iso);
    int days_ok = 0;
    for (;;) {
        params.as_of = FormatIsoDate(cursor);
        try {
            static_cast<void>(BuildVolSurfaceEod(params));
            ++days_ok;
        } catch (const ValidationError& ex) {
            Logger::NumWarn("Skipping {}: {}.", params.as_of, ex.what());
        }
        if (cursor.year == end.year && cursor.month == end.month && cursor.day == end.day) {
            break;
        }
        cursor = schedule::FromQuantLibDate(schedule::ToQuantLibDate(cursor) + 1);
    }

    Logger::NumInfo("build-vol-surface-eod finished: {} day(s) built in range {}..{}.", days_ok, from_iso, to_iso);
    return 0;
}

}  // namespace numeraire::database
