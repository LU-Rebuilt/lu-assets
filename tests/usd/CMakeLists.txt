add_executable(test_usd
    test_nif_usd_roundtrip.cpp
    test_kf_usd.cpp
)

# lu_assets_usd already brings in OpenUSD (public link deps).
target_link_libraries(test_usd PRIVATE lu_assets_usd GTest::gtest_main)
gtest_discover_tests(test_usd)
