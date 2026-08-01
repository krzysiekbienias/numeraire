#pragma once

#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>

#include <cstddef>
#include <vector>

namespace numeraire::quant {

/// One node of a recombining CRR tree (educational / audit snapshot).
/// `up_moves` = number of up moves from the root along the path to this node
/// (equivalently index \(i\) with spot \(S_0 u^{i} d^{step-i}\)).
struct CrrTreeNode {
    int step = 0;
    int up_moves = 0;
    double spot = 0.0;
    double intrinsic = 0.0;
    double continuation = 0.0;  // at expiry equals intrinsic
    double value = 0.0;
    bool early_exercise = false;
};

/// Full tree dump for a small \(n\) (lab / audit). `nodes` ordered by
/// increasing `step`, then `up_moves`.
struct CrrTreeDump {
    std::size_t n_steps = 0;
    double dt = 0.0;
    double u = 0.0;
    double d = 0.0;
    double p_up = 0.0;
    double discount = 0.0;
    double npv = 0.0;
    std::vector<CrrTreeNode> nodes;
};

/// Cox–Ross–Rubinstein recombining tree for equity vanilla (continuous \(r\), \(q\)).
/// Tree state is a single flat `std::vector` of size `n_steps + 1` (in-place rollback).
/// Amounts are **per one unit of underlying**.
[[nodiscard]] double CoxRossRubinsteinVanillaPrice(OptionType option_type,
                                                   ExerciseStyle exercise,
                                                   double spot,
                                                   double strike,
                                                   double risk_free_rate,
                                                   double dividend_yield,
                                                   double volatility,
                                                   double time_to_expiry_years,
                                                   std::size_t n_steps);

/// Same economics as `CoxRossRubinsteinVanillaPrice`, but keeps every node.
/// Intended for small `n_steps` (lab tree drawing / audit). Root NPV matches the
/// scalar pricer when a proper tree is built (positive vol, \(\tau>0\), \(n\ge 1\)).
[[nodiscard]] CrrTreeDump CoxRossRubinsteinVanillaTree(OptionType option_type,
                                                       ExerciseStyle exercise,
                                                       double spot,
                                                       double strike,
                                                       double risk_free_rate,
                                                       double dividend_yield,
                                                       double volatility,
                                                       double time_to_expiry_years,
                                                       std::size_t n_steps);

}  // namespace numeraire::quant
