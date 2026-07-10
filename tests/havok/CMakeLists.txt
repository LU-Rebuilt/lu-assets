add_executable(test_havok
    test_hkx.cpp
    test_hkx_geometry.cpp
    test_hkx_packfile.cpp
    test_hkx_tagged.cpp
)

target_link_libraries(test_havok PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_havok)
