qt_add_executable(NLHC WIN32
    main.cpp
    MainWindow.cpp
    MainWindow.h
    I18n.cpp
    I18n.h
    ShipListModel.cpp
    ShipListModel.h
    HullEditorPanel.cpp
    HullEditorPanel.h
    StylePanel.cpp
    StylePanel.h
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
target_compile_definitions(NLHC PRIVATE NLHC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(NLHC PRIVATE NLHCCore Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL)
install(TARGETS NLHC RUNTIME DESTINATION bin)

# Qt's own deploy script generator emits unquoted absolute paths, which breaks for
# source trees containing spaces. Drive windeployqt through a configured script instead.
if(WIN32)
    get_target_property(_nlhc_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_nlhc_qt_bin "${_nlhc_qmake}" DIRECTORY)
    find_program(NLHC_WINDEPLOYQT windeployqt HINTS "${_nlhc_qt_bin}" REQUIRED)
    configure_file(deploy_windows.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/deploy_windows.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_windows.cmake")
endif()
