add_library(numeraire_utils STATIC
        log_level.cpp
        logger.cpp
)

target_include_directories(numeraire_utils
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_utils
        PUBLIC
        spdlog::spdlog
)
