#pragma once

#include <vector>

namespace numeraire::quant {

// ── Continuous compounding ──────────────────────────────────────────────
double DiscountFactorFromContinuousZero(double zero_rate, double time_years);
double ContinuousZeroFromDiscountFactor(double discount_factor, double time_years);

// // ── Simple (money-market / Libor) ───────────────────────────────────────
// double DiscountFactorFromSimpleRate(double simple_rate, double time_years);
// double SimpleRateFromDiscountFactor(double discount_factor, double time_years);

// // ── Annual / periodic compounding ───────────────────────────────────────
// double DiscountFactorFromAnnualRate(double annual_rate, double time_years);
// double AnnualRateFromDiscountFactor(double discount_factor, double time_years);
// double DiscountFactorFromCompoundedRate(double rate, double time_years, int compounding_frequency);
// double CompoundedRateFromDiscountFactor(double discount_factor, double time_years, int compounding_frequency);

// // ── Compounding convention conversions ──────────────────────────────────
// double ContinuousToSimple(double continuous_rate, double time_years);
// double SimpleToContinuous(double simple_rate, double time_years);
// double ContinuousToCompounded(double continuous_rate, int compounding_frequency);
// double CompoundedToContinuous(double compounded_rate, int compounding_frequency);
// double ConvertCompoundingFrequency(double rate, int from_frequency, int to_frequency);

// // ── Forward rates ───────────────────────────────────────────────────────
// double ForwardDiscountFactor(double df_start, double df_end);
// double ForwardRateContinuous(double df_start, double df_end, double tau);
// double ForwardRateSimple(double df_start, double df_end, double tau);
// double InstantaneousForwardRate(double zero_t1, double t1, double zero_t2, double t2);

// // ── Zero <-> par / swap ─────────────────────────────────────────────────
// double ParRateFromDiscountFactors(const std::vector<double>& discount_factors, const std::vector<double>& accruals);
// double AnnuityFromDiscountFactors(const std::vector<double>& discount_factors, const std::vector<double>& accruals);
}  // namespace numeraire::quant
