#pragma once

#include <string>

namespace numeraire::database {

/// One leg row to persist after a booking pricer run (`execution_price` = per-share `pv_unit`).
struct TradeLegBookingUpdate {
    std::string leg_id;
    double execution_price{};
};

}  // namespace numeraire::database
