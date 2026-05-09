#include <numeraire/utils/logger.hpp>

using numeraire::utils::Logger;

int main() {
    Logger::Init();
    Logger::NumInfo("dev_main sandbox starting (DEV_MAIN_BUILD).");
    return 0;
}
