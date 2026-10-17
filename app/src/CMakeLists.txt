configure_file(core/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.h @ONLY)

add_library(vialarmed_core STATIC
    core/appinfo.cpp
    core/appinfo.h
    core/bytes.h
    core/lzma.cpp
    core/lzma.h
    core/paths.h
    keycodes/anykeycode.cpp
    keycodes/anykeycode.h
    keycodes/keycodes.cpp
    keycodes/keycodes.h
    keycodes/tables.h
    keycodes/tables_gen.cpp
    hid/devices.cpp
    hid/devices.h
    hid/link.cpp
    hid/link.h
    hid/monitor.cpp
    hid/monitor.h
    hid/rawhid.cpp
    hid/rawhid.h
    hid/session.cpp
    hid/session.h
    model/definition.cpp
    model/definition.h
    model/macro.cpp
    model/macro.h
    model/dynamic.cpp
    model/dynamic.h
    model/keyboard.cpp
    model/keyboard.h
    model/qmksettings.cpp
    model/qmksettings.h
    model/hud.cpp
    model/hud.h
    model/theme.cpp
    model/theme.h
    model/kle.cpp
    model/kle.h
    render/lighting.cpp
    render/lighting.h
    render/hudrender.cpp
    render/hudrender.h
    protocol/armed.cpp
    protocol/armed.h
    protocol/vial.cpp
    protocol/vial.h
    protocol/vibl.cpp
    protocol/vibl.h
)
target_include_directories(vialarmed_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(vialarmed_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent ${VIAL_ARMED_HIDAPI} ${VIAL_ARMED_LZMA})
if(VIAL_ARMED_UDEV_FOUND)
    target_compile_definitions(vialarmed_core PRIVATE VIAL_ARMED_UDEV)
    target_link_libraries(vialarmed_core PRIVATE PkgConfig::UDEV)
endif()

qt_add_executable(vial-armed WIN32 MACOSX_BUNDLE
    main.cpp
    ui/mainwindow.cpp
    ui/mainwindow.h
)
target_link_libraries(vial-armed PRIVATE vialarmed_core Qt6::Widgets Qt6::Svg Qt6::Network)

qt_add_resources(vialarmed_core resources
    PREFIX "/"
    BASE ${VIAL_ARMED_PYTHON_ROOT}
    FILES
        ${VIAL_ARMED_PYTHON_ROOT}/icons/icon.svg
        ${VIAL_ARMED_PYTHON_ROOT}/resources/base/fonts/Rajdhani-Bold.ttf
        ${VIAL_ARMED_PYTHON_ROOT}/resources/base/fonts/ShareTechMono-Regular.ttf
        ${VIAL_ARMED_PYTHON_ROOT}/resources/base/armed/down.svg
        ${VIAL_ARMED_PYTHON_ROOT}/resources/base/armed/down-small.svg
        ${VIAL_ARMED_PYTHON_ROOT}/resources/base/armed/up.svg
        ${VIAL_ARMED_PYTHON_ROOT}/resources/base/qmk_settings.json
)

set_target_properties(vial-armed PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Vial Armed"
    MACOSX_BUNDLE_GUI_IDENTIFIER "today.vial.armed"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

install(TARGETS vial-armed BUNDLE DESTINATION . RUNTIME DESTINATION bin)
