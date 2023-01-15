project(foo_dsp_eqsplit)
cmake_minimum_required(VERSION 3.23)
set(CMAKE_CXX_STANDARD 20)

# The package boost is compatible with built-in CMake targets:
# 
#     find_package(Boost REQUIRED [COMPONENTS <libs>...])
#     target_link_libraries(main PRIVATE Boost::boost Boost::<lib1> Boost::<lib2> ...)
# 
# foobar2000-sdk provides CMake targets:
# 
#     # this is heuristically generated, and may not be correct
#     find_package(unofficial-foobar2000-sdk CONFIG REQUIRED)
#     # note: 2 additional targets are not displayed.
#     target_link_libraries(main PRIVATE unofficial::foobar2000-sdk::SDK unofficial::foobar2000-sdk::pfc unofficial::foobar2000-sdk::shared unofficial::foobar2000-sdk::helpers)

# Boost
find_package(Boost REQUIRED COMPONENTS serialization)

# foobar2000
find_package(unofficial-foobar2000-sdk CONFIG REQUIRED)

add_compile_options(-MP)

add_compile_definitions(UNICODE _UNICODE)

add_library(foo_dsp_eqsplit SHARED main.cc)
target_link_libraries(foo_dsp_eqsplit PRIVATE
    "shlwapi"
    Boost::boost Boost::serialization
    unofficial::foobar2000-sdk::foobar2000_component_client
    unofficial::foobar2000-sdk::helpers
    unofficial::foobar2000-sdk::pfc
    unofficial::foobar2000-sdk::SDK
    unofficial::foobar2000-sdk::shared
)
