#include <numeraire/simulation/gabillon_spec.hpp>

#include <numeraire/database/calibration_snapshot_read.hpp>
#include <numeraire/database/futures_pillar_returns.hpp>
#include <numeraire/database/underlying_daily_closes.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace numeraire::simulation {
namespace {

constexpr const char* kShortSuffix = "_SHORT";
constexpr const char* kLongSuffix = "_LONG";

/// Volatility of every calibrated factor, keyed by factor id.
[[nodiscard]] std::unordered_map<std::string, double> VolByFactorId(
        const database::CalibrationSnapshotRead& snapshot) {
    std::unordered_map<std::string, double> out;
    out.reserve(snapshot.factor_ids.size());
    for (std::size_t i = 0; i < snapshot.factor_ids.size(); ++i) {
        out.emplace(snapshot.factor_ids[i], snapshot.volatilities[i]);
    }
    return out;
}

}  // namespace

std::optional<GabillonCurveDynamics> TryLoadGabillonCurveDynamics(const std::string& database_file_path,
                                                                  const std::string_view scope_key,
                                                                  const std::string_view as_of,
                                                                  const std::string_view product_code) {
    const std::optional<database::CalibrationSnapshotRead> snapshot =
            database::TryLoadLatestCalibrationSnapshot(database_file_path, scope_key, as_of,
                                                       database::calibration_model::kGabillon2F,
                                                       database::calibration_source::kFit);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    const std::unordered_map<std::string, double> vol_by_factor = VolByFactorId(*snapshot);
    const std::string code(product_code);
    const auto short_it = vol_by_factor.find(code + kShortSuffix);
    const auto long_it = vol_by_factor.find(code + kLongSuffix);
    const std::optional<double> mean_reversion = snapshot->TryGetParam("mean_reversion", product_code);
    if (short_it == vol_by_factor.end() || long_it == vol_by_factor.end() || !mean_reversion.has_value()) {
        return std::nullopt;
    }

    return GabillonCurveDynamics{
            .params =
                    GabillonCurveParams{
                            .product_code = code,
                            .mean_reversion = *mean_reversion,
                            .short_factor_vol = short_it->second,
                            .long_factor_vol = long_it->second,
                    },
            .factor_correlation = snapshot->TryGetParam("factor_correlation", product_code).value_or(0.0),
    };
}

std::optional<GabillonSimulationSpec> TryLoadGabillonSpecFromDatabase(const std::string& database_file_path,
                                                                      const std::string_view scope_key,
                                                                      const std::string_view valuation_as_of) {
    const std::optional<database::CalibrationSnapshotRead> snapshot =
            database::TryLoadLatestCalibrationSnapshot(database_file_path, scope_key, valuation_as_of,
                                                       database::calibration_model::kGabillon2F,
                                                       database::calibration_source::kFit);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    const std::vector<database::BookFuturesContract> contracts =
            database::ListBookFuturesContracts(database_file_path, scope_key);
    if (contracts.empty()) {
        throw ValidationError("TryLoadGabillonSpecFromDatabase: no LIVE futures legs in scope_key=" +
                              std::string(scope_key) + "; a Gabillon fit only carries commodity curves.");
    }

    GabillonSimulationSpec spec;
    spec.calibration_as_of = snapshot->as_of;
    spec.calibration_id = snapshot->calibration_id;

    // The shock vector has to keep the snapshot's factor order, because that is the order
    // its Cholesky was built in. Curves are therefore indexed by where their short factor
    // sits, not by the order contracts happen to arrive in.
    const std::unordered_map<std::string, double> vol_by_factor = VolByFactorId(*snapshot);
    std::map<std::string, std::size_t> curve_index_by_product;
    for (std::size_t i = 0; i < snapshot->factor_ids.size(); ++i) {
        const std::string& factor_id = snapshot->factor_ids[i];
        if (!factor_id.ends_with(kShortSuffix)) {
            continue;
        }
        const std::string product_code = factor_id.substr(0, factor_id.size() - std::strlen(kShortSuffix));
        const auto long_it = vol_by_factor.find(product_code + kLongSuffix);
        if (long_it == vol_by_factor.end()) {
            std::string message = "TryLoadGabillonSpecFromDatabase: calibration ";
            message.append(std::to_string(snapshot->calibration_id)).append(" has ").append(factor_id);
            message.append(" but no matching ").append(product_code).append(kLongSuffix).append(".");
            throw ValidationError(message);
        }
        const std::optional<double> mean_reversion = snapshot->TryGetParam("mean_reversion", product_code);
        if (!mean_reversion.has_value() || !(*mean_reversion > 0.0)) {
            throw ValidationError("TryLoadGabillonSpecFromDatabase: calibration " +
                                  std::to_string(snapshot->calibration_id) +
                                  " has no positive mean_reversion for " + product_code + ".");
        }

        curve_index_by_product.emplace(product_code, spec.curves.size());
        spec.curves.push_back(GabillonCurveParams{
                .product_code = product_code,
                .mean_reversion = *mean_reversion,
                .short_factor_vol = snapshot->volatilities[i],
                .long_factor_vol = long_it->second,
        });
    }

    if (spec.curves.size() * 2U != snapshot->factor_ids.size() || snapshot->cholesky.n != spec.NumShocks()) {
        throw ValidationError("TryLoadGabillonSpecFromDatabase: calibration " +
                              std::to_string(snapshot->calibration_id) +
                              " does not hold exactly one short and one long factor per curve.");
    }

    const schedule::Date valuation_date = schedule::ParseIsoDate(std::string(valuation_as_of));
    std::map<std::string, std::vector<database::FuturesCurveQuote>> curve_by_product;
    for (const database::BookFuturesContract& contract : contracts) {
        if (contract.settlement_date.empty()) {
            throw ValidationError("TryLoadGabillonSpecFromDatabase: contract " + contract.contract_ticker +
                                  " has no settlement date.");
        }
        const auto curve_it = curve_index_by_product.find(contract.product_code);
        if (curve_it == curve_index_by_product.end()) {
            throw ValidationError("TryLoadGabillonSpecFromDatabase: leg contract " + contract.contract_ticker +
                                  " belongs to " + contract.product_code +
                                  ", which is not in calibration " + std::to_string(snapshot->calibration_id) +
                                  " (recalibrate the book).");
        }
        if (!curve_by_product.contains(contract.product_code)) {
            curve_by_product.emplace(contract.product_code,
                                     database::LoadFuturesCurveSnapshot(database_file_path,
                                                                        contract.product_code, valuation_as_of));
        }

        double anchor = 0.0;
        for (const database::FuturesCurveQuote& quote : curve_by_product.at(contract.product_code)) {
            if (quote.ticker == contract.contract_ticker) {
                anchor = quote.price;
                break;
            }
        }
        if (!(anchor > 0.0)) {
            throw ValidationError("TryLoadGabillonSpecFromDatabase: contract " + contract.contract_ticker +
                                  " has no settle on " + std::string(valuation_as_of) +
                                  "; paths must start from the price the leg is marked at.");
        }

        spec.contracts.push_back(GabillonContract{
                .contract_ticker = contract.contract_ticker,
                .curve_index = curve_it->second,
                .settlement_years = schedule::Act365FixedYearFraction(
                        valuation_date, schedule::ParseIsoDate(contract.settlement_date)),
                .anchor_price = anchor,
        });
    }

    spec.cholesky = snapshot->cholesky;
    return spec;
}

}  // namespace numeraire::simulation
