add_executable(test_forkparticle test_psb.cpp test_psb_simulator.cpp test_effect_file.cpp)
target_link_libraries(test_forkparticle PRIVATE lu_assets GTest::gtest_main)
target_compile_definitions(test_forkparticle PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
gtest_discover_tests(test_forkparticle)
