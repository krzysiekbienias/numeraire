#include <fstream>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>

namespace numeraire::utils {
namespace {

[[nodiscard]] const nlohmann::json& Navigate(const nlohmann::json& node,
                                             std::string_view segment,
                                             const std::string& full_path) {
    if (!node.is_object()) {
        throw numeraire::ConfigError("Config: '" + full_path + "' is not an object at segment '" +
                                     std::string(segment) + "'");
    }
    if (!node.contains(segment)) {
        throw numeraire::ConfigError("Config: missing key '" + std::string(segment) + "' in path '" + full_path + "'");
    }
    return node.at(segment);
}

}  // namespace

Config Config::Load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw numeraire::ConfigError("Config: file does not exist: " + path.string());
    }
    std::ifstream in(path);
    if (!in) {
        throw numeraire::ConfigError("Config: cannot open file: " + path.string());
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::parse_error& ex) {
        throw numeraire::ConfigError(std::string("Config: JSON parse error: ") + ex.what());
    }
    return Config(std::move(root));
}

const nlohmann::json& Config::RequireAt(std::string_view dotted_path) const {
    const std::string full_path(dotted_path);
    if (dotted_path.empty()) {
        throw numeraire::ConfigError("Config: empty dotted path");
    }

    const nlohmann::json* current = &root_;
    std::string_view remaining = dotted_path;

    while (!remaining.empty()) {
        std::string_view segment;
        const std::size_t dot = remaining.find('.');
        if (dot == std::string_view::npos) {
            segment = remaining;
            remaining = {};
        } else {
            segment = remaining.substr(0, dot);
            remaining.remove_prefix(dot + 1);
        }
        if (segment.empty()) {
            throw numeraire::ConfigError("Config: empty segment in path '" + full_path + "'");
        }
        current = &Navigate(*current, segment, full_path);
    }

    if (current->is_null()) {
        throw numeraire::ConfigError("Config: null value at path '" + full_path + "'");
    }
    return *current;
}

std::string Config::RequireString(std::string_view dotted_path) const {
    const nlohmann::json& value = RequireAt(dotted_path);
    if (!value.is_string()) {
        throw numeraire::ConfigError("Config: value at '" + std::string(dotted_path) + "' is not a string");
    }
    return value.get<std::string>();
}

}  // namespace numeraire::utils
