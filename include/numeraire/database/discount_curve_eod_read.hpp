#pragma once

#include <numeraire/quant/discount_curve_bootstrap.hpp>

#include <optional>
#include <string>
#include <vector>

namespace numeraire::database {

/// Bootstrapped discount curve loaded from `discount_curve_eod` / `discount_curve_point_eod`.
struct DiscountCurveEodRead {
    std::string curve_id;
    std::string as_of;
    std::string interpolation_method{"linear_zero_rate"};
    std::vector<quant::BootstrappedCurvePoint> pillars;
};

/// True when a discount curve header exists for the exact `(curve_id, as_of)` key.
[[nodiscard]] bool HasDiscountCurveEod(const std::string& database_file_path,
                                       std::string_view curve_id,
                                       std::string_view as_of_iso_yyyy_mm_dd);

/// Loads the latest curve with `as_of <= requested_as_of`, or std::nullopt if none.
[[nodiscard]] std::optional<DiscountCurveEodRead> TryLoadLatestDiscountCurveEod(
        const std::string& database_file_path,
        std::string_view curve_id,
        std::string_view on_or_before_as_of_iso_yyyy_mm_dd);

}  // namespace numeraire::database
