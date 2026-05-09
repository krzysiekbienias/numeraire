#include <numeraire/utils/logger.hpp>

using numeraire::utils::Logger;

int main() {
    Logger::Init();
    Logger::NumInfo("Numeraire++ app starting (placeholder entry point).");
    return 0;
}
