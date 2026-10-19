# Package-root entry point: starts bin/NLHC.exe relative to its own location,
# which a .lnk cannot do portably.
if(NOT WIN32)
    return()
endif()

set(NLHC_LAUNCHER_NAME "Liner Hull Customizer")
set(NLHC_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")

# windres breaks on source paths containing spaces, so both the .ico and the
# generated .rc are staged in the build tree first.
set(NLHC_LAUNCHER_ICON "${CMAKE_CURRENT_BINARY_DIR}/launcher.ico")
add_custom_command(
    OUTPUT "${NLHC_LAUNCHER_ICON}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/app/nlhc.ico" "${NLHC_LAUNCHER_ICON}"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/app/nlhc.ico"
    COMMENT "Staging launcher.ico"
    VERBATIM
)
set(NLHC_ICON_FILE "${NLHC_LAUNCHER_ICON}")

configure_file(launcher.rc.in "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" @ONLY)
set(NLHC_LAUNCHER_RC "${CMAKE_CURRENT_BINARY_DIR}/launcher_rc.obj")
add_custom_command(
    OUTPUT "${NLHC_LAUNCHER_RC}"
    COMMAND "${CMAKE_RC_COMPILER}" -i "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc"
            -o "${NLHC_LAUNCHER_RC}" -O coff
    DEPENDS launcher.rc.in "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" "${NLHC_LAUNCHER_ICON}"
    COMMENT "Compiling launcher.rc (icon + version info)"
    VERBATIM
)
set_source_files_properties("${NLHC_LAUNCHER_RC}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)

add_executable(NLHCLauncher WIN32 launcher.cpp "${NLHC_LAUNCHER_RC}")
set_target_properties(NLHCLauncher PROPERTIES OUTPUT_NAME "${NLHC_LAUNCHER_NAME}")
target_link_libraries(NLHCLauncher PRIVATE shell32 user32)
nlhc_sign_after_build(NLHCLauncher)
install(TARGETS NLHCLauncher RUNTIME DESTINATION .)
