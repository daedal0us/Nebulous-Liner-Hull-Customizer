enable_testing()

function(nlhc_add_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Gui Qt6::Xml NLHCCore)
    target_compile_definitions(${name} PRIVATE NLHC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nlhc_add_test(tst_version tst_version.cpp)
nlhc_add_test(tst_fleet tst_fleet.cpp)
nlhc_add_test(tst_package tst_package.cpp MockAssets.h)
nlhc_add_test(tst_assembler tst_assembler.cpp MockAssets.h)
nlhc_add_test(tst_validate tst_validate.cpp MockAssets.h)
nlhc_add_test(tst_assets tst_assets.cpp)

qt_add_executable(tst_render_smoke tst_render_smoke.cpp)
target_link_libraries(tst_render_smoke PRIVATE Qt6::Test NLHCRender)
add_test(NAME tst_render_smoke COMMAND tst_render_smoke)
