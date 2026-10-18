qt_add_executable(NLHC WIN32
    main.cpp
    MainWindow.cpp
    MainWindow.h
)
target_link_libraries(NLHC PRIVATE NLHCCore Qt6::Widgets)
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
