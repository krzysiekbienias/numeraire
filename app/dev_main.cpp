#include <numeraire/utils/logger.hpp>

int main() {
    numeraire::utils::Logger::Init();
    NUM_INFO("dev_main sandbox starting (DEV_MAIN_BUILD).");
    return 0;
}
