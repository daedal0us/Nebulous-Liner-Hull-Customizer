# Rendering stack shared by the application and the render smoke test.
add_library(NLHCRender STATIC
    DemoAssembly.cpp
    DemoAssembly.h
    view/Camera.cpp
    view/Camera.h
    view/HullRenderer.cpp
    view/HullRenderer.h
    view/HullViewport.cpp
    view/HullViewport.h
    view/OffscreenRenderer.cpp
    view/OffscreenRenderer.h
    view/RenderStyle.h
)
target_include_directories(NLHCRender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/core/include)
target_link_libraries(NLHCRender PUBLIC NLHCCore Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL)

get_target_property(_nlhc_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_nlhc_qt_bin "${_nlhc_qmake}" DIRECTORY)

# qt_add_translations cannot cope with source paths containing spaces, so drive
# lrelease and the resource file by hand.
set(NLHC_TS_FILE "${CMAKE_SOURCE_DIR}/i18n/nlhc_zh_CN.ts")
set(NLHC_QM_DIR "${CMAKE_BINARY_DIR}/i18n")
set(NLHC_QM_FILE "${NLHC_QM_DIR}/nlhc_zh_CN.qm")
find_program(NLHC_LRELEASE lrelease HINTS "${_nlhc_qt_bin}" REQUIRED)
add_custom_command(
    OUTPUT "${NLHC_QM_FILE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NLHC_QM_DIR}"
    COMMAND "${NLHC_LRELEASE}" "${NLHC_TS_FILE}" -qm "${NLHC_QM_FILE}"
    DEPENDS "${NLHC_TS_FILE}"
    COMMENT "Compiling nlhc_zh_CN.qm"
    VERBATIM
)
add_custom_target(nlhc_translations DEPENDS "${NLHC_QM_FILE}")
set(NLHC_QRC_FILE "${CMAKE_CURRENT_BINARY_DIR}/nlhc_translations.qrc")
file(WRITE "${NLHC_QRC_FILE}"
    "<RCC><qresource prefix=\"/\"><file alias=\"i18n/nlhc_zh_CN.qm\">${NLHC_QM_FILE}</file></qresource></RCC>\n")

# Application manifest (asInvoker + per-monitor DPI + long paths) and version
# resource. Without a manifest Windows applies installer heuristics and shows
# "unknown publisher" elevation prompts for a binary that never asks for them.
#
# windres is driven by hand: the RC language rule passes the target's include
# directories as -I flags, and this source tree lives under a path with spaces,
# which splits those flags mid-path. The .rc is generated into the build tree
# (no spaces) and needs no -I at all.
if(WIN32)
    if(NOT CMAKE_RC_COMPILER)
        find_program(NLHC_WINDRES windres REQUIRED)
        set(CMAKE_RC_COMPILER "${NLHC_WINDRES}")
    endif()
    set(NLHC_MANIFEST_VERSION "${PROJECT_VERSION}.0")
    set(NLHC_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(NLHC_ICON_FILE "${CMAKE_CURRENT_BINARY_DIR}/nlhc.ico")
    configure_file(nlhc.rc.in "${CMAKE_CURRENT_BINARY_DIR}/nlhc.rc" @ONLY)
    set(NLHC_RESOURCE_FILE "${CMAKE_CURRENT_BINARY_DIR}/nlhc_rc.obj")
    add_custom_command(
        OUTPUT "${NLHC_RESOURCE_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/nlhc.ico" "${NLHC_ICON_FILE}"
        COMMAND "${CMAKE_RC_COMPILER}" -i "${CMAKE_CURRENT_BINARY_DIR}/nlhc.rc" -o "${NLHC_RESOURCE_FILE}" -O coff
        DEPENDS nlhc.rc.in nlhc.ico "${CMAKE_CURRENT_BINARY_DIR}/nlhc.rc"
        COMMENT "Compiling nlhc.rc (manifest + version info + icon)"
        VERBATIM
    )
    set_source_files_properties("${NLHC_RESOURCE_FILE}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
else()
    set(NLHC_RESOURCE_FILE "")
endif()

qt_add_executable(NLHC WIN32
    main.cpp
    MainWindow.cpp
    MainWindow.h
    I18n.cpp
    I18n.h
    ShipListModel.cpp
    ShipListModel.h
    SteamLocator.cpp
    SteamLocator.h
    HullEditorPanel.cpp
    HullEditorPanel.h
    StylePanel.cpp
    StylePanel.h
    DiagnosticText.cpp
    DiagnosticText.h
    ${NLHC_QRC_FILE}
    ${NLHC_RESOURCE_FILE}
)
add_dependencies(NLHC nlhc_translations)
target_compile_definitions(NLHC PRIVATE NLHC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(NLHC PRIVATE NLHCRender)
nlhc_sign_after_build(NLHC)
install(TARGETS NLHC RUNTIME DESTINATION bin)

# Qt's own deploy script generator emits unquoted absolute paths, which breaks for
# source trees containing spaces. Drive windeployqt through a configured script instead.
if(WIN32)
    find_program(NLHC_WINDEPLOYQT windeployqt HINTS "${_nlhc_qt_bin}" REQUIRED)
    configure_file(deploy_windows.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/deploy_windows.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_windows.cmake")
endif()
