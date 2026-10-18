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

enable_testing()
add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(tests)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION assets)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/samples/ DESTINATION samples)
