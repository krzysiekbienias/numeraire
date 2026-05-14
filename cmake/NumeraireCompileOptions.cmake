# Numeraire++ — global compile options
#
# Centralizes language standard, warnings, debug/release flags and OS-specific
# tweaks. Included once from the top-level CMakeLists.txt; affects every
# target defined afterwards (via add_compile_options / add_link_options).
#
# Per-target overrides should still go into each module's CMakeLists.txt via
# target_compile_options / target_compile_features.

# --- C++ standard -----------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Default build type -----------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug|Release|RelWithDebInfo|MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# --- compile_commands.json --------------------------------------------------
# Required for clangd / clang-tidy editor integration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

# --- Warnings ---------------------------------------------------------------
# Strict but not absurd. Treated as warnings (not errors) until CI lands; flip
# to -Werror once we have a stable green pipeline.
add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wdouble-promotion
        -Wformat=2
        -Wno-unused-parameter
)

# --- Per-config flags -------------------------------------------------------
add_compile_options(
        "$<$<CONFIG:Debug>:-O0;-g3;-fno-omit-frame-pointer>"
        "$<$<CONFIG:RelWithDebInfo>:-O2;-g>"
        "$<$<CONFIG:Release>:-O3;-DNDEBUG>"
        "$<$<CONFIG:MinSizeRel>:-Os;-DNDEBUG>"
)

# Disable IPO/LTO globally; can be turned on per-target if profiling justifies.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

# --- Compiler-specific ------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Better debug info for LLDB stepping into stdlib / templates.
    add_link_options("$<$<CONFIG:Debug>:-fstandalone-debug>")
endif()

# --- macOS-specific ---------------------------------------------------------
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
    endif()
    # Make Homebrew packages discoverable by find_package() out of the box.
    if(EXISTS "/opt/homebrew" AND NOT "/opt/homebrew" IN_LIST CMAKE_PREFIX_PATH)
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    endif()
    # Use libc++ explicitly in Debug for predictable LLDB type rendering.
    add_compile_options("$<$<CONFIG:Debug>:-stdlib=libc++>")
endif()
