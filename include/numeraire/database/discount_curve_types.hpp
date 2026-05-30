#pragma once

#include <numeraire/quant/discount_curve_bootstrap.hpp>

#include <string>
#include <vector>

namespace numeraire::database {

struct ParCurveEodHeader {
    std::string curve_id;
    std::string as_of;
    std::string currency;
    std::string day_count;
    std::string session_calendar;
};

struct ParCurvePillarInput {
    std::string tenor;
    int tenor_days{0};
    std::string instrument_type;
    double quoted_rate{0.0};
};

struct DiscountCurveEodHeaderWrite {
    std::string curve_id;
    std::string as_of;
    std::string source_par_curve_id;
    std::string source_par_as_of;
    std::string currency{"USD"};
    std::string day_count{"Actual365Fixed"};
    std::string session_calendar{"America/New_York"};
    std::string interpolation_method{"linear_zero_rate"};
    std::string bootstrap_engine{"deposit_swap_v1"};
    std::string batch_run_id;
    std::string ingested_at;
};

struct DiscountCurvePointWrite {
    std::string tenor;
    double time_years{0.0};
    double zero_rate{0.0};
    double discount_factor{0.0};
};

struct DiscountCurveBuildStats {
    int pillars_loaded{0};
    int pillars_written{0};
};

/// Map `par_curve_point_eod.instrument_type` → bootstrap pillar kind.
[[nodiscard]] quant::CurvePillarKind ParCurveInstrumentToKind(const std::string& instrument_type);

[[nodiscard]] std::vector<quant::CurvePillarQuote> ToCurvePillarQuotes(
        const std::vector<ParCurvePillarInput>& pillars);

}  // namespace numeraire::database
