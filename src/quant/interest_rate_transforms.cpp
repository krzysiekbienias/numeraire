#include <cmath>
#include <numeraire/quant/interest_rate_transforms.hpp>

namespace numeraire::quant {

double DiscountFactorFromContinuousZero(double zero_rate, double time_years) {
    return std::exp(-zero_rate * time_years);
}

double ContinuousZeroFromDiscountFactor(double discount_factor, double time_years) {
    return -std::log(discount_factor) / time_years;
}

}  // namespace numeraire::quant
