#pragma once

#include <numeraire/enums/option_type.hpp>

#include <string>

namespace numeraire::database {

/// One option row loaded for IV inversion (no SQLite types).
struct VolSurfaceOptionQuoteInput {
    std::string option_ticker;
    double close{0.0};
    double strike{0.0};
    std::string expiration_date;
    OptionType option_type{OptionType::kCall};
    std::string exercise_style;
};

struct VolSurfaceEodHeaderWrite {
    std::string underlying_id;
    std::string as_of;
    std::string surface_kind{"implied_bs_eod"};
    std::string coordinate_system{"strike_expiry"};
    double spot_used{0.0};
    double risk_free_rate{0.0};
    double dividend_yield{0.0};
    std::string model{"black_scholes_european"};
    std::string price_source{"option_daily_price_eod.close"};
    std::string currency{"USD"};
    std::string ingested_at;
    std::string batch_run_id;
};

struct VolSurfacePointWrite {
    std::string expiration_date;
    double years_to_maturity{0.0};
    double strike{0.0};
    std::string contract_type;
    double implied_vol{0.0};
    std::string source_option_ticker;
    double input_price{0.0};
    std::string quality;
};

struct VolSurfaceBuildStats {
    int quotes_loaded{0};
    int points_written{0};
    int skipped_invalid_inputs{0};
    int skipped_below_intrinsic{0};
    int skipped_no_convergence{0};
    int skipped_non_european{0};
    int skipped_bad_catalog{0};
    int skipped_duplicate_slice{0};
};

}  // namespace numeraire::database
