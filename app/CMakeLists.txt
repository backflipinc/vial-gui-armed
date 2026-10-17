cmake_minimum_required(VERSION 3.21)

include(cmake/Version.cmake)
vial_armed_git_version(VIAL_ARMED_VERSION VIAL_ARMED_VERSION_NUMERIC)

project(vial-armed VERSION ${VIAL_ARMED_VERSION_NUMERIC} LANGUAGES C CXX)
# packaging names its files after this
file(WRITE ${CMAKE_BINARY_DIR}/version.txt ${VIAL_ARMED_VERSION})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /permissive-)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VIAL_ARMED_TESTS "Build the tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Network Concurrent)
qt_standard_project_setup()

include(cmake/Dependencies.cmake)


add_subdirectory(src)

if(VIAL_ARMED_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
