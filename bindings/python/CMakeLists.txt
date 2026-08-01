# Numeraire++ → Python extension `numeraire_cpp` (Quant Lab / sandbox).
#
# Output lands in `web/` so `uv run` / Django can `import numeraire_cpp` when
# the process cwd or PYTHONPATH includes `web/`.

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

include(FetchContent)
FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.13.6
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(numeraire_cpp MODULE numeraire_cpp_module.cpp)

target_link_libraries(numeraire_cpp
        PRIVATE
        numeraire_pricers
        numeraire_products
        numeraire_schedule
        numeraire_simulation
)

# Match the Django / uv venv layout.
set(_numeraire_py_out "${CMAKE_SOURCE_DIR}/web")
set_target_properties(numeraire_cpp PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${_numeraire_py_out}"
        RUNTIME_OUTPUT_DIRECTORY "${_numeraire_py_out}"
)

# Multi-config generators (Xcode / VS)
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_cfg}" _cfg_u)
    set_target_properties(numeraire_cpp PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${_cfg_u} "${_numeraire_py_out}"
            RUNTIME_OUTPUT_DIRECTORY_${_cfg_u} "${_numeraire_py_out}"
    )
endforeach()

message(STATUS "Python bindings: numeraire_cpp → ${_numeraire_py_out} (Python ${Python3_VERSION})")
