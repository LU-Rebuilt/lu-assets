add_executable(test_fmod test_fev.cpp test_fsb.cpp)
target_link_libraries(test_fmod PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_fmod)
