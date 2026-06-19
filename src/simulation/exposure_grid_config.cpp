#include <numeraire/simulation/exposure_grid_config.hpp>

#include <numeraire/utils/config.hpp>
#include <numeraire/utils/exception.hpp>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace numeraire::simulation {
namespace {

using numeraire::ConfigError;

}  // namespace

ExposureGridConfig LoadExposureGridConfig(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigError("simulation_exposure_grid: cannot open " + path.string());
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception& ex) {
        throw ConfigError(std::string{"simulation_exposure_grid: parse error: "} + ex.what());
    }

    ExposureGridConfig cfg;
    cfg.schema_version = root.value("schema_version", "0");
    cfg.name = root.value("name", "unnamed");

    if (!root.contains("conventions") || !root["conventions"].is_object()) {
        throw ConfigError("simulation_exposure_grid: missing conventions object.");
    }
    const auto& conv = root["conventions"];
    cfg.conventions.include_valuation_date = conv.value("include_valuation_date", true);
    cfg.conventions.clip_to_book_max_expiry = conv.value("clip_to_book_max_expiry", true);
    cfg.conventions.min_horizon_days = conv.value("min_horizon_days", 365);
    if (cfg.conventions.min_horizon_days < 0) {
        throw ConfigError("simulation_exposure_grid: min_horizon_days must be >= 0.");
    }

    if (!root.contains("exposure_pillars") || !root["exposure_pillars"].is_array()) {
        throw ConfigError("simulation_exposure_grid: missing exposure_pillars array.");
    }
    for (const auto& pillar_json : root["exposure_pillars"]) {
        if (!pillar_json.is_object() || !pillar_json.contains("id") ||
            !pillar_json.contains("target_dte_days")) {
            throw ConfigError("simulation_exposure_grid: invalid exposure_pillars entry.");
        }
        ExposurePillarConfig pillar;
        pillar.id = pillar_json["id"].get<std::string>();
        pillar.target_dte_days = pillar_json["target_dte_days"].get<int>();
        pillar.tier = pillar_json.value("tier", "core");
        if (pillar.target_dte_days < 0) {
            throw ConfigError("simulation_exposure_grid: target_dte_days must be >= 0.");
        }
        cfg.exposure_pillars.push_back(std::move(pillar));
    }
    if (cfg.exposure_pillars.empty()) {
        throw ConfigError("simulation_exposure_grid: exposure_pillars is empty.");
    }

    return cfg;
}

std::optional<std::filesystem::path> ExposureGridConfigPathFromDefaults(
        const std::filesystem::path& default_json_path) {
    const numeraire::utils::Config defaults = numeraire::utils::Config::Load(default_json_path);
    if (!defaults.Root().contains("simulation") || !defaults.Root()["simulation"].is_object()) {
        return std::nullopt;
    }
    const auto& sim = defaults.Root()["simulation"];
    if (!sim.contains("exposure_grid_config") || !sim["exposure_grid_config"].is_string()) {
        return std::nullopt;
    }
    return std::filesystem::path(sim["exposure_grid_config"].get<std::string>());
}

}  // namespace numeraire::simulation
