add_executable(test_microsoft
    test_dds.cpp
    test_tga.cpp
    test_fxo.cpp
)

target_link_libraries(test_microsoft PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_microsoft)
