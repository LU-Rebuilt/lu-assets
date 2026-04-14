add_executable(test_netdevil
    test_binary_reader.cpp
    test_sd0.cpp
    test_pk.cpp
    test_fdb.cpp
    test_luz.cpp
    test_lvl.cpp
    test_terrain.cpp
    test_lutriggers.cpp
    test_aud.cpp
    test_zal.cpp
    test_ast.cpp
    test_scm.cpp
    test_ldf.cpp
    test_pki.cpp
)

target_link_libraries(test_netdevil PRIVATE lu_assets GTest::gtest_main)
gtest_discover_tests(test_netdevil)
