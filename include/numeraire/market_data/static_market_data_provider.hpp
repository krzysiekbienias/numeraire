#pragma once

#include <memory>
#include <numeraire/market_data/imarket_data_provider.hpp>
#include <numeraire/market_data/market_snapshot.hpp>

namespace numeraire::market_data {

/// Provider that serves a fixed in-memory `MarketSnapshot` (e.g. parsed from
/// JSON, loaded from SQLite, or built in tests). No I/O in this type — callers
/// construct the snapshot elsewhere.
class StaticMarketDataProvider final : public IMarketDataProvider {
public:
    explicit StaticMarketDataProvider(MarketSnapshot snapshot);

    [[nodiscard]] std::unique_ptr<core::IMarketData> CreateMarketData() const override;

    [[nodiscard]] const MarketSnapshot& Snapshot() const noexcept { return snapshot_; }

private:
    MarketSnapshot snapshot_;
};

}  // namespace numeraire::market_data
