configure_file(core/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.h @ONLY)

add_library(vialarmed_core STATIC
    agent/hudagent.cpp
    agent/hudagent.h
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
    model/layoutoptions.cpp
    model/layoutoptions.h
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
    model/hudpresets.cpp
    model/hudpresets.h
    model/screenmedia.cpp
    model/screenmedia.h
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

# everything but main(): the app, and the UI tests
qt_add_library(vialarmed_ui STATIC
    app/agenthost.cpp
    app/agenthost.h
    app/controller.cpp
    app/firmwareflasher.cpp
    app/firmwareflasher.h
    pages/vialpages.cpp
    pages/vialpages.h
    app/controller.h
    pages/armeddevice.cpp
    pages/armeddevice.h
    pages/armedhud.cpp
    pages/armedhud.h
    pages/armedscreen.cpp
    pages/armedscreen.h
    pages/armedthemes.cpp
    pages/armedthemes.h
    ui/widgets/themecontrols.cpp
    ui/widgets/themecontrols.h
    pages/armedlighting.cpp
    pages/armedlighting.h
    ui/widgets/armedcontrols.cpp
    ui/widgets/armedcontrols.h
    pages/entrypages.cpp
    pages/entrypages.h
    pages/keymappage.cpp
    pages/keymappage.h
    pages/macropage.cpp
    pages/macropage.h
    recorder/macrorecorder.cpp
    recorder/macrorecorder.h
    ui/widgets/keybutton.cpp
    ui/widgets/keybutton.h
    pages/layoutpage.cpp
    pages/layoutpage.h
    ui/anykeycodedialog.cpp
    ui/anykeycodedialog.h
    ui/keycodedisplay.cpp
    ui/keycodedisplay.h
    ui/unlocker.cpp
    ui/unlocker.h
    ui/widgets/hudcanvas.cpp
    ui/widgets/hudcanvas.h
    ui/widgets/keyboardview.cpp
    ui/widgets/keyboardview.h
    ui/widgets/keycodepalette.cpp
    ui/widgets/keycodepalette.h
    ui/mainwindow.cpp
    ui/mainwindow.h
    ui/page.h
    ui/pages.cpp
    ui/pages.h
    ui/sidenav.cpp
    ui/sidenav.h
    ui/theme.cpp
    ui/theme.h
    ui/toast.cpp
    ui/toast.h
)
target_link_libraries(vialarmed_ui PUBLIC vialarmed_core Qt6::Widgets Qt6::Svg Qt6::Network)

set(VIAL_ARMED_PKG ${CMAKE_CURRENT_SOURCE_DIR}/../packaging)
set(VIAL_ARMED_RES ${CMAKE_CURRENT_SOURCE_DIR}/../resources)
if(WIN32)
    set(app_icon ${VIAL_ARMED_PKG}/windows/vial-armed.rc)
elseif(APPLE)
    # made from icons/mac/*.png ahead of time: iconutil failed on the CI runner
    set(app_icon ${VIAL_ARMED_RES}/icons/vial-armed.icns)
    set_source_files_properties(${app_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
qt_add_executable(vial-armed WIN32 MACOSX_BUNDLE main.cpp ${app_icon})
target_link_libraries(vial-armed PRIVATE vialarmed_ui)

# resource paths are the Python app's, which the code still uses
set(VIAL_ARMED_RES_FILES
    icons/icon.svg=icons/icon.svg
    fonts/Rajdhani-Bold.ttf=resources/base/fonts/Rajdhani-Bold.ttf
    fonts/ShareTechMono-Regular.ttf=resources/base/fonts/ShareTechMono-Regular.ttf
    armed/down.svg=resources/base/armed/down.svg
    armed/down-small.svg=resources/base/armed/down-small.svg
    armed/up.svg=resources/base/armed/up.svg
    qmk_settings.json=resources/base/qmk_settings.json)
set(res_files)
foreach(pair ${VIAL_ARMED_RES_FILES})
    string(REPLACE "=" ";" parts ${pair})
    list(GET parts 0 file)
    list(GET parts 1 alias)
    set_source_files_properties(${VIAL_ARMED_RES}/${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
    list(APPEND res_files ${VIAL_ARMED_RES}/${file})
endforeach()
qt_add_resources(vialarmed_core resources PREFIX "/" FILES ${res_files})

set_target_properties(vial-armed PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Vial Armed"
    MACOSX_BUNDLE_GUI_IDENTIFIER "today.vial.armed"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE vial-armed.icns)

install(TARGETS vial-armed BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(FILES ${VIAL_ARMED_PKG}/linux/io.github.backflipinc.vial_armed.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${VIAL_ARMED_RES}/icons/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME vial-armed.svg)
    install(FILES ${VIAL_ARMED_RES}/icons/256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME vial-armed.png)
    install(FILES ${VIAL_ARMED_PKG}/linux/70-vial-armed.rules DESTINATION lib/udev/rules.d)
    string(TIMESTAMP VIAL_ARMED_DATE "%Y-%m-%d" UTC)
    # .appdata.xml: the name appimagetool looks for (AppStream takes either)
    set(metainfo ${CMAKE_CURRENT_BINARY_DIR}/io.github.backflipinc.vial_armed.appdata.xml)
    configure_file(${VIAL_ARMED_PKG}/linux/io.github.backflipinc.vial_armed.metainfo.xml.in ${metainfo} @ONLY)
    install(FILES ${metainfo} DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()
