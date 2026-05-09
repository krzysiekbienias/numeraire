add_library(numeraire_schedule STATIC
        date.cpp
        schedule_generator.cpp
        schedule_quantlib.cpp
)

target_include_directories(numeraire_schedule
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_schedule
        PUBLIC
        numeraire_utils
)
