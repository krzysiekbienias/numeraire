#pragma once

#include <numeraire/schedule/schedule_config.hpp>

#include <optional>
#include <string>

namespace numeraire::utils {
class Config;
}  // namespace numeraire::utils

namespace numeraire::schedule {

/// Optional string fields from a trade row (`schedule_*` columns). Empty or
/// whitespace-only strings are treated like "not set" and fall back to
/// [`configs/default.json`](../configs/default.json) `schedule` defaults via
/// [`Config`](../numeraire/utils/config.hpp).
struct TradeScheduleOverrides {
    std::optional<std::string> calendar_type;
    std::optional<std::string> frequency;
    std::optional<std::string> date_convention;
    std::optional<std::string> generation_rule;
    std::optional<std::string> day_count;
};

/// Merges DB overrides with JSON defaults: per field, use a non-empty override
/// string (PascalCase, same tokens as `configs/default.json`) else
/// `RequireString` on the matching `schedule.default_*` path.
class ScheduleConfigResolver {
   public:
    [[nodiscard]] static ScheduleConfig Resolve(const TradeScheduleOverrides& overrides,
                                                const utils::Config& config);
};

}  // namespace numeraire::schedule
