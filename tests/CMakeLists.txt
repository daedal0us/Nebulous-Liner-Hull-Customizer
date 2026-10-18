enable_testing()

function(nlhc_add_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test NLHCCore)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nlhc_add_test(tst_version tst_version.cpp)
