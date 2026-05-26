#include <cpr/cpr.h>
#include <cpr/util.h>

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <numeraire/market_data_providers/polygon_ingest_common.hpp>
#include <numeraire/utils/logger.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace numeraire::market_data_providers::polygon_ingest {

using numeraire::utils::Logger;

namespace {

constexpr int kBasicTierSleepSec = 13;
constexpr int kStarterTierSleepSec = 0;

[[nodiscard]] bool EqualsAsciiIgnoreCase(const char* a, const char* b) noexcept {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

[[nodiscard]] int ClampSleepSec(const long v) {
    if (v < 0) {
        return kBasicTierSleepSec;
    }
    if (v > 3600) {
        return 3600;
    }
    return static_cast<int>(v);
}

[[nodiscard]] bool TryParseIntEnv(const char* name, int& out) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const long v = std::strtol(raw, &end, 10);
    if (end == raw) {
        return false;
    }
    out = ClampSleepSec(v);
    return true;
}

[[nodiscard]] bool TryParsePlanEnv(const char* name, int& out) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    if (EqualsAsciiIgnoreCase(raw, "starter") || EqualsAsciiIgnoreCase(raw, "unlimited")) {
        out = kStarterTierSleepSec;
        return true;
    }
    if (EqualsAsciiIgnoreCase(raw, "basic") || EqualsAsciiIgnoreCase(raw, "free")) {
        out = kBasicTierSleepSec;
        return true;
    }
    return false;
}

[[nodiscard]] int ResolveSleepSec(const char* explicit_sec_env,
                                  const char* plan_env,
                                  const char* legacy_env) {
    int sleep_sec = kBasicTierSleepSec;
    if (TryParseIntEnv(explicit_sec_env, sleep_sec)) {
        return sleep_sec;
    }
    if (TryParsePlanEnv(plan_env, sleep_sec)) {
        return sleep_sec;
    }
    if (TryParseIntEnv(legacy_env, sleep_sec)) {
        return sleep_sec;
    }
    return kBasicTierSleepSec;
}

}  // namespace

int SleepSecAfterPolygonEquityCall() noexcept {
    return ResolveSleepSec("NUMERAIRE_POLYGON_EQUITY_SLEEP_SEC",
                           "NUMERAIRE_POLYGON_EQUITY_PLAN",
                           "NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL");
}

int SleepSecAfterPolygonOptionsCall() noexcept {
    return ResolveSleepSec("NUMERAIRE_POLYGON_OPTIONS_SLEEP_SEC",
                           "NUMERAIRE_POLYGON_OPTIONS_PLAN",
                           "NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL");
}

int SleepSecAfterPolygonCall() noexcept {
    return SleepSecAfterPolygonEquityCall();
}

std::string PolygonBaseUrl() {
    const char* raw = std::getenv("POLYGON_BASE_URL");
    if (raw == nullptr || raw[0] == '\0') {
        return "https://api.polygon.io";
    }
    std::string s(raw);
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

const char* PolygonApiKey() {
    return std::getenv("POLYGON_API_KEY");
}

bool LooksIsoDate(const std::string& s) {
    if (s.size() != 10) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 4 || i == 7) {
            if (c != '-') {
                return false;
            }
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string AsOfIsoFromPolygonBarMs(const std::int64_t t_ms) {
    const auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(
            std::chrono::milliseconds{t_ms});
    const time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[16]{};
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

std::string IsoUtcNow() {
    const time_t tt = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32]{};
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

void ThrottleAfterCall(const int sleep_sec) {
    if (sleep_sec > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(sleep_sec));
    }
}

std::string UrlWithApiKey(const std::string& url, const char* api_key) {
    if (url.find("apiKey=") != std::string::npos) {
        return url;
    }
    std::string out = url;
    out += (url.find('?') == std::string::npos) ? '?' : '&';
    out += "apiKey=";
    out += cpr::util::urlEncode(std::string(api_key));
    return out;
}

std::string DataSourceLabelForBaseUrl(const std::string& base_url) {
    // Rebranded host; same vendor/data — keep distinct label for ops debugging.
    if (base_url.find("massive.com") != std::string::npos) {
        return "massive";
    }
    return "polygon";
}

int FetchJsonPage(const std::string& url,
                  const char* api_key,
                  const int throttle_sec,
                  nlohmann::json& out_j,
                  std::string& next_url_full) {
    next_url_full.clear();

    const std::string request_url = UrlWithApiKey(url, api_key);
    const auto resp = cpr::Get(cpr::Url{request_url});

    ThrottleAfterCall(throttle_sec);

    if (resp.status_code != 200) {
        Logger::NumError("Polygon HTTP {} for URL {}", resp.status_code, url);
        return 1;
    }

    try {
        out_j = nlohmann::json::parse(resp.text);
    } catch (const std::exception& e) {
        Logger::NumError("Polygon JSON parse: {}", e.what());
        return 1;
    }

    if (out_j.contains("status") && out_j["status"].is_string()) {
        const std::string st = out_j["status"].get<std::string>();
        if (!st.empty() && st != "OK") {
            Logger::NumError("Polygon status={} body_snip={}", st, resp.text.substr(0, 200));
            return 1;
        }
    }

    if (out_j.contains("next_url") && out_j["next_url"].is_string()) {
        next_url_full = out_j["next_url"].get<std::string>();
        if (!next_url_full.empty()) {
            next_url_full = UrlWithApiKey(next_url_full, api_key);
        }
    }
    return 0;
}

}  // namespace numeraire::market_data_providers::polygon_ingest
