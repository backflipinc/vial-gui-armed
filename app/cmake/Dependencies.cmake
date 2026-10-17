include(FetchContent)

# hidapi: on Linux the hidraw backend, so the kernel driver stays attached to the raw HID interface
find_package(hidapi 0.13 CONFIG QUIET)
if(NOT hidapi_FOUND)
    set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(hidapi
        GIT_REPOSITORY https://github.com/libusb/hidapi.git
        GIT_TAG hidapi-0.15.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(hidapi)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
endif()
if(TARGET hidapi::hidraw)
    set(VIAL_ARMED_HIDAPI hidapi::hidraw)
else()
    set(VIAL_ARMED_HIDAPI hidapi::hidapi)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UDEV IMPORTED_TARGET libudev)
    set(VIAL_ARMED_UDEV_FOUND ${UDEV_FOUND})
endif()

# on Windows a runner's stray liblzma may come without headers: always build our own
if(NOT WIN32)
    find_package(LibLZMA QUIET)
endif()
if(LibLZMA_FOUND)
    set(VIAL_ARMED_LZMA LibLZMA::LibLZMA)
else()
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    set(XZ_TOOL_XZ OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_XZDEC OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_LZMADEC OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_SCRIPTS OFF CACHE BOOL "" FORCE)
    set(XZ_DOC OFF CACHE BOOL "" FORCE)
    set(XZ_NLS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF)
    FetchContent_Declare(xz
        GIT_REPOSITORY https://github.com/tukaani-project/xz.git
        GIT_TAG v5.8.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(xz)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
    # xz's own LibLZMA::LibLZMA is imported in its directory only
    set(VIAL_ARMED_LZMA liblzma)
    target_include_directories(liblzma INTERFACE $<BUILD_INTERFACE:${xz_SOURCE_DIR}/src/liblzma/api>)
    # a static liblzma, not the DLL import declarations
    target_compile_definitions(liblzma INTERFACE LZMA_API_STATIC)
endif()
