cmake_minimum_required(VERSION 3.21)
project(NebulousLinerHullCustomizer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Xml Widgets OpenGLWidgets OpenGL Concurrent Test LinguistTools)
qt_standard_project_setup()

# A relink discards the Authenticode signature, and Windows then reports the
# publisher as unknown. Re-sign every freshly linked binary when a certificate
# thumbprint is configured (-DNLHC_SIGN_CERT=<thumbprint>, or via
# scripts/build.ps1 -Sign). Without a thumbprint this is a no-op.
set(NLHC_SIGN_CERT "" CACHE STRING "Code-signing certificate thumbprint; signs built binaries")
function(nlhc_sign_after_build target)
    if(NOT WIN32 OR NOT NLHC_SIGN_CERT)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/sign-one.ps1"
                -Path "$<TARGET_FILE:${target}>"
                -Thumbprint "${NLHC_SIGN_CERT}"
        COMMENT "Signing ${target}"
        VERBATIM)
endfunction()

enable_testing()
add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(src/launcher)
add_subdirectory(tests)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION assets)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/samples/ DESTINATION samples)
