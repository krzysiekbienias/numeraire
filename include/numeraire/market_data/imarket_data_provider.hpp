#pragma once

#include <numeraire/core/imarket_data.hpp>

#include <memory>

namespace numeraire::market_data {

/// Abstraction over any source that can materialize an `IMarketData` view
/// (static files, SQLite, live Polygon, …). Polygon-backed implementations
/// stay outside the core sprint scope; this interface is the seam.
class IMarketDataProvider {
   public:
    virtual ~IMarketDataProvider() = default;

    IMarketDataProvider(const IMarketDataProvider&) = delete;
    IMarketDataProvider& operator=(const IMarketDataProvider&) = delete;
    IMarketDataProvider(IMarketDataProvider&&) = delete;
    IMarketDataProvider& operator=(IMarketDataProvider&&) = delete;

    [[nodiscard]] virtual std::unique_ptr<core::IMarketData> CreateMarketData() const = 0;

   protected:
    IMarketDataProvider() = default;
};

}  // namespace numeraire::market_data
