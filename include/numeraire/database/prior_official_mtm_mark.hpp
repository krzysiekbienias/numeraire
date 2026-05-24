#pragma once

#include <string>

namespace numeraire::database {

/// Official prior EOD mark from `trade_leg_mtm_eod` (not the append-only archive).
struct PriorOfficialMtmMark {
    std::string as_of;
    double pv_total{};
};

}  // namespace numeraire::database
