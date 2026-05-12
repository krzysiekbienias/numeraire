#pragma once

#include <numeraire/enums/currency.hpp>

#include <optional>
#include <string>
#include <utility>

namespace numeraire::core {

/// Optional first-order sensitivities from a single pricing run. Omitted when
/// the pricer did not compute greeks or the instrument has no natural mapping.
struct PricingGreeks {
    std::optional<double> delta;
    std::optional<double> gamma;
    std::optional<double> vega;
    std::optional<double> theta;
    std::optional<double> rho;
};

/// Non-NPV context for audit, reporting, and debugging (not a substitute for
/// full `IProduct` / `IMarketData` state).
struct PricingMetadata {
    /// Valuation as-of (e.g. ISO-8601 `YYYY-MM-DD`) until core uses a shared
    /// result date type.
    std::optional<std::string> as_of;
    std::optional<Currency> result_currency;
    /// Free-form notes: MC paths, convergence, data-quality warnings, …
    std::optional<std::string> diagnostics;
};

/// Outcome of `PricingEngine::Price` / `IPricer::Price` for one run. `Npv()` may
/// be empty when valuation did not produce a number (error path, unsupported
/// combination, or deferred calculation).
class PricingResult {
   public:
    PricingResult() = default;

    explicit PricingResult(const double npv) : npv_(npv) {}

    [[nodiscard]] const std::optional<double>& Npv() const { return npv_; }

    [[nodiscard]] std::optional<double>& Npv() { return npv_; }

    void SetNpv(std::optional<double> npv) { npv_ = npv; }

    void SetNpv(const double npv) { npv_ = npv; }

    [[nodiscard]] const std::optional<PricingGreeks>& Greeks() const { return greeks_; }

    [[nodiscard]] std::optional<PricingGreeks>& Greeks() { return greeks_; }

    void SetGreeks(std::optional<PricingGreeks> greeks) { greeks_ = greeks; }

    [[nodiscard]] const std::optional<PricingMetadata>& Metadata() const { return metadata_; }

    [[nodiscard]] std::optional<PricingMetadata>& Metadata() { return metadata_; }

    void SetMetadata(std::optional<PricingMetadata> metadata) {
        metadata_ = std::move(metadata);
    }

   private:
    std::optional<double> npv_;
    std::optional<PricingGreeks> greeks_;
    std::optional<PricingMetadata> metadata_;
};

}  // namespace numeraire::core
