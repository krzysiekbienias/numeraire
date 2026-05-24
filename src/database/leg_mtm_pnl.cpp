#include <numeraire/database/leg_mtm_pnl.hpp>
#include <numeraire/database/leg_pv.hpp>

namespace numeraire::database {

double LegBookedMark(const TradeLegCatalogRow& row) noexcept {
    return LegPvTotal(row.leg.direction, row.leg.quantity, row.equity.contract_size, row.leg.execution_price);
}

double LegCommissionOrZero(const TradeLegDto& leg) noexcept {
    if (leg.commission.has_value()) {
        return *leg.commission;
    }
    return 0.0;
}

double ResolvePvTotalPrevForDaily(const std::optional<PriorOfficialMtmMark>& prior_official_mark,
                                  const double booked_mark) noexcept {
    if (prior_official_mark.has_value()) {
        return prior_official_mark->pv_total;
    }
    return booked_mark;
}

}  // namespace numeraire::database
