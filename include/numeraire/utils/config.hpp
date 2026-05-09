#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace numeraire::utils {

/// Thin wrapper around a JSON configuration file (committed defaults live in
/// `configs/default.json`). Throws numeraire::ConfigError on I/O or parse
/// failures and on missing dotted paths for the Require* helpers.
class Config {
   public:
    [[nodiscard]] static Config Load(const std::filesystem::path& path);

    [[nodiscard]] const nlohmann::json& Root() const noexcept { return root_; }

    /// Returns a const reference to the JSON value at a dotted path such as
    /// `logging.level`. Throws ConfigError if any segment is missing or null.
    [[nodiscard]] const nlohmann::json& RequireAt(std::string_view dotted_path) const;

    [[nodiscard]] std::string RequireString(std::string_view dotted_path) const;

   private:
    explicit Config(nlohmann::json root) : root_(std::move(root)) {}

    nlohmann::json root_;
};

}  // namespace numeraire::utils
