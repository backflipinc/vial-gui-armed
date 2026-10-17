configure_file(core/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.h @ONLY)

add_library(vialarmed_core STATIC
    core/appinfo.cpp
    core/appinfo.h
)
target_include_directories(vialarmed_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(vialarmed_core PUBLIC Qt6::Core Qt6::Gui ${VIAL_ARMED_HIDAPI} ${VIAL_ARMED_LZMA})

qt_add_executable(vial-armed WIN32 MACOSX_BUNDLE
    main.cpp
    ui/mainwindow.cpp
    ui/mainwindow.h
)
target_link_libraries(vial-armed PRIVATE vialarmed_core Qt6::Widgets Qt6::Svg Qt6::Network Qt6::Concurrent)

qt_add_resources(vial-armed resources
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
