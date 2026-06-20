#include <numeraire/simulation/path_pricing_rules.hpp>

#include <numeraire/schedule/date.hpp>

namespace numeraire::simulation {

bool IsLegActiveOnGridNode(const schedule::Date& node_date,
                           const schedule::Date& expiry_date) noexcept {
    return schedule::Act365FixedYearFraction(node_date, expiry_date) >= 0.0;
}

}  // namespace numeraire::simulation
