#include <numeraire/database/leg_pv.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>

namespace numeraire::database {

double PositionSign(const numeraire::PositionDirection direction) noexcept {
    return direction == numeraire::PositionDirection::kShort ? -1.0 : 1.0;
}

double LegPvTotal(const numeraire::PositionDirection direction,
                  const double quantity,
                  const double contract_size,
                  const double pv_unit) noexcept {
    return PositionSign(direction) * quantity * contract_size * pv_unit;
}

double LegDeltaTotal(numeraire::PositionDirection direction,
                     double quantity,
                     double contract_size,
                     double delta) noexcept {
    return PositionSign(direction) * quantity * contract_size * delta;
}

double LegGammaTotal(numeraire::PositionDirection direction,
                     double quantity,
                     double contract_size,
                     double gamma) noexcept {
    return PositionSign(direction) * quantity * contract_size * gamma;
}

double LegVegaTotal(numeraire::PositionDirection direction,
                    double quantity,
                    double contract_size,
                    double vega) noexcept {
    return PositionSign(direction) * quantity * contract_size * vega;
}

double LegThetaTotal(numeraire::PositionDirection direction,
                     double quantity,
                     double contract_size,
                     double theta) noexcept {
    return PositionSign(direction) * quantity * contract_size * theta;
}

double LegRhoTotal(numeraire::PositionDirection direction, double quantity, double contract_size, double rho) noexcept {
    return PositionSign(direction) * quantity * contract_size * rho;
}

void RequirePositiveContractSize(const double contract_size, const std::string_view leg_id) {
    if (contract_size <= 0.0) {
        throw numeraire::ValidationError("Contract size must be positive for leg " + std::string(leg_id));
    }
}

void RequirePositiveQuantity(const double quantity, const std::string_view leg_id) {
    if (quantity <= 0.0) {
        throw numeraire::ValidationError("Quantity must be positive for leg " + std::string(leg_id));
    }
}

}  // namespace numeraire::database
