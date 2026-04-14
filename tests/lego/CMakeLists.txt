add_executable(test_lego
    test_lxfml.cpp
    test_brick_geometry.cpp
    test_brick_colors.cpp
)

target_link_libraries(test_lego PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_lego)
