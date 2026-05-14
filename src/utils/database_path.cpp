#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>

#include <cstdlib>
#include <cstring>

namespace numeraire::utils {

std::filesystem::path ResolveDatabasePath(const Config& cfg) {
    if (const char* from_env = std::getenv("NUMERAIRE_DB_PATH");
        from_env != nullptr && std::strlen(from_env) != 0) {
        return std::filesystem::path(from_env);
    }
    return cfg.RequireString("database.path");
}

}  // namespace numeraire::utils
