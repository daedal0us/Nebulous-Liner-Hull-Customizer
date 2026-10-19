qt_add_library(NLHCCore SHARED
    include/nlhc/Export.h
    include/nlhc/Version.h
    include/nlhc/Diagnostics.h
    include/nlhc/asset/HullPackage.h
    include/nlhc/asset/MeshData.h
    include/nlhc/asset/Piece.h
    include/nlhc/fleet/FleetDocument.h
    include/nlhc/fleet/HullConfig.h
    include/nlhc/fleet/ShipRecord.h
    include/nlhc/hull/DressingLayout.h
    include/nlhc/hull/HullAssembler.h
    include/nlhc/hull/Validate.h
    src/Version.cpp
    src/Diagnostics.cpp
    src/MeshData.cpp
    src/asset/HullPackage.cpp
    src/fleet/FleetDocument.cpp
    src/hull/DressingLayout.cpp
    src/hull/HullAssembler.cpp
    src/hull/Validate.cpp
)
target_include_directories(NLHCCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(NLHCCore PRIVATE Qt6::Core Qt6::Gui Qt6::Xml)
target_compile_definitions(NLHCCore PRIVATE NLHC_VERSION="${PROJECT_VERSION}")
set_target_properties(NLHCCore PROPERTIES OUTPUT_NAME NLHCCore)
nlhc_sign_after_build(NLHCCore)
install(TARGETS NLHCCore RUNTIME DESTINATION bin)
