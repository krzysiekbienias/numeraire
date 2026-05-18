#pragma once

#include <optional>
#include <string>

namespace numeraire::database {

/// One row for [`trade_leg_mtm_eod`](../../../../sql/schema_v1.sql) before INSERT/UPSERT.
struct TradeLegMtmEodRow {
    std::string as_of;
    std::string session_calendar{"America/New_York"};
    std::string trade_id;
    std::string leg_id;
    std::optional<std::string> batch_run_id;

    double underlying_spot{};
    double risk_free_rate{};
    double dividend_yield{};
    double implied_vol_used{};
    double years_to_maturity{};

    std::string numeraire_currency{"USD"};
    double pv_unit{};
    double pv_total{};
    std::optional<double> pnl_daily;
    std::optional<double> pnl_inception;

    double delta{};
    double gamma{};
    double vega{};
    double theta{};
    double rho{};

    std::string pricing_engine;
    /// Stored verbatim; if empty, repository substitutes UTC timestamp when upserting.
    std::string calculated_at;
    std::string remarks;
};

}  // namespace numeraire::database
