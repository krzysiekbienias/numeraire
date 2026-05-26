#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace numeraire::market_data_providers::polygon_ingest {

/// Throttle after each Polygon HTTP call (equity + index `v2/aggs` jobs).
/// Env (first match wins): `NUMERAIRE_POLYGON_EQUITY_SLEEP_SEC`,
/// `NUMERAIRE_POLYGON_EQUITY_PLAN` (`basic` → 13 s, `starter`/`unlimited` → 0),
/// legacy `NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL`, else 13.
[[nodiscard]] int SleepSecAfterPolygonEquityCall() noexcept;

/// Throttle for options jobs (`v3/reference/options/*`, future option EOD aggs).
/// Env: `NUMERAIRE_POLYGON_OPTIONS_SLEEP_SEC`, `NUMERAIRE_POLYGON_OPTIONS_PLAN`, then legacy.
[[nodiscard]] int SleepSecAfterPolygonOptionsCall() noexcept;

/// @deprecated Prefer `SleepSecAfterPolygonEquityCall` / `SleepSecAfterPolygonOptionsCall`.
/// Same resolution as equity (legacy alias).
[[nodiscard]] int SleepSecAfterPolygonCall() noexcept;

[[nodiscard]] std::string PolygonBaseUrl();

[[nodiscard]] const char* PolygonApiKey();

[[nodiscard]] bool LooksIsoDate(const std::string& s);

[[nodiscard]] std::string AsOfIsoFromPolygonBarMs(std::int64_t t_ms);

[[nodiscard]] std::string IsoUtcNow();

void ThrottleAfterCall(int sleep_sec);

[[nodiscard]] std::string UrlWithApiKey(const std::string& url, const char* api_key);

[[nodiscard]] std::string DataSourceLabelForBaseUrl(const std::string& base_url);

/// GET \p url (apiKey appended safely), throttle, parse JSON.
/// Accepts missing `status` or `"status":"OK"` (same API on api.polygon.io and api.massive.com).
/// \return 0 on success, 1 on failure (logs via Logger).
[[nodiscard]] int FetchJsonPage(const std::string& url,
                                const char* api_key,
                                int throttle_sec,
                                nlohmann::json& out_j,
                                std::string& next_url_full);

}  // namespace numeraire::market_data_providers::polygon_ingest
