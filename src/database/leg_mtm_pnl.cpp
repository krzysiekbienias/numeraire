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

double LegPnlDaily(const double pv_total, const double pv_total_prev) noexcept {
    return pv_total - pv_total_prev;
}

double LegPnlInception(const double pv_total, const double booked_mark, const double commission) noexcept {
    return pv_total - booked_mark - commission;
}

}  // namespace numeraire::database
