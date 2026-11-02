add_executable(test_scaleform
    test_gfx.cpp
)

target_link_libraries(test_scaleform PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_scaleform DISCOVERY_TIMEOUT ${LU_ASSETS_GTEST_DISCOVERY_TIMEOUT})
