add_executable(test_gamebryo
    test_nif.cpp
    test_kfm.cpp
    test_settings.cpp
)

target_link_libraries(test_gamebryo PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_gamebryo)
