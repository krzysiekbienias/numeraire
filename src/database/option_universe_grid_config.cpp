#include <numeraire/database/option_universe_grid_config.hpp>

#include <numeraire/utils/exception.hpp>

#include <filesystem>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace numeraire::database {
namespace {

using numeraire::ConfigError;

[[nodiscard]] std::vector<double> MergeOtmPercents(const nlohmann::json& otm_obj) {
    std::set<double> uniq;
    const auto ingest = [&](const char* key) {
        if (!otm_obj.contains(key) || !otm_obj[key].is_array()) {
            return;
        }
        for (const auto& v : otm_obj[key]) {
            if (v.is_number()) {
                uniq.insert(v.get<double>());
            }
        }
    };
    ingest("atm_dense");
    ingest("mid");
    ingest("wing");
    if (uniq.empty()) {
        throw ConfigError("option_universe_grid: otm_moneyness_percent has no numeric levels.");
    }
    return {uniq.begin(), uniq.end()};
}

}  // namespace

OptionUniverseGridConfig LoadOptionUniverseGridConfig(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigError("option_universe_grid: cannot open " + path.string());
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception& ex) {
        throw ConfigError(std::string{"option_universe_grid: parse error: "} + ex.what());
    }

    OptionUniverseGridConfig cfg;
    cfg.schema_version = root.value("schema_version", "0");
    cfg.name = root.value("name", "unnamed");

    if (!root.contains("conventions") || !root["conventions"].is_object()) {
        throw ConfigError("option_universe_grid: missing conventions object.");
    }
    cfg.skip_point_if_strike_gap_exceeds_ratio =
            root["conventions"].value("skip_point_if_strike_gap_exceeds_ratio", 0.75);

    if (!root.contains("expiry_pillars") || !root["expiry_pillars"].is_array()) {
        throw ConfigError("option_universe_grid: missing expiry_pillars array.");
    }
    for (const auto& p : root["expiry_pillars"]) {
        if (!p.is_object() || !p.contains("id") || !p.contains("target_dte_days")) {
            throw ConfigError("option_universe_grid: invalid expiry_pillars entry.");
        }
        ExpiryPillarConfig pillar;
        pillar.id = p["id"].get<std::string>();
        pillar.target_dte_days = p["target_dte_days"].get<int>();
        pillar.tier = p.value("tier", "core");
        cfg.expiry_pillars.push_back(std::move(pillar));
    }
    if (cfg.expiry_pillars.empty()) {
        throw ConfigError("option_universe_grid: expiry_pillars is empty.");
    }

    if (!root.contains("otm_moneyness_percent") || !root["otm_moneyness_percent"].is_object()) {
        throw ConfigError("option_universe_grid: missing otm_moneyness_percent object.");
    }
    cfg.otm_moneyness_percent = MergeOtmPercents(root["otm_moneyness_percent"]);

    return cfg;
}

}  // namespace numeraire::database
