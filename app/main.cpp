#include <numeraire/utils/config.hpp>
#include <numeraire/utils/env_loader.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>

using numeraire::utils::Config;
using numeraire::utils::EnvLoader;
using numeraire::utils::Logger;

int main() {
    EnvLoader env;
    env.LoadFromFile(".env");
    env.ApplyToEnvironment();

    Logger::Init();
    try {
        const Config cfg = Config::Load("configs/default.json");
        Logger::NumInfo("Numeraire++ starting (config version={}).", cfg.RequireString("version"));
    } catch (const numeraire::ConfigError& ex) {
        Logger::NumWarn("Config load failed: {}", ex.what());
    }
    Logger::NumInfo("Numeraire++ app running.");
    return 0;
}
