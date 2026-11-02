add_executable(test_common
    test_primitives.cpp
)
target_link_libraries(test_common PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_common DISCOVERY_TIMEOUT ${LU_ASSETS_GTEST_DISCOVERY_TIMEOUT})
