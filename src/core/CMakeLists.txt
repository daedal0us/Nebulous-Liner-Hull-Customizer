qt_add_library(NLHCCore SHARED
    include/nlhc/Export.h
    include/nlhc/Version.h
    src/Version.cpp
)
target_include_directories(NLHCCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(NLHCCore PRIVATE Qt6::Core Qt6::Gui Qt6::Xml)
target_compile_definitions(NLHCCore PRIVATE NLHC_VERSION="${PROJECT_VERSION}")
set_target_properties(NLHCCore PROPERTIES OUTPUT_NAME NLHCCore)
install(TARGETS NLHCCore RUNTIME DESTINATION bin)
