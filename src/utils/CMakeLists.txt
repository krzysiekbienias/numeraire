add_library(numeraire_utils STATIC
        config.cpp
        database_path.cpp
        env_loader.cpp
        log_level.cpp
        logger.cpp
        quantlib_bridge.cpp
)

target_include_directories(numeraire_utils
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_utils
        PUBLIC
        numeraire_enums
        QuantLib::QuantLib
        spdlog::spdlog
        fmt::fmt
        nlohmann_json::nlohmann_json
)
