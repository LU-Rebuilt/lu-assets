add_subdirectory(common)
add_subdirectory(netdevil)
add_subdirectory(gamebryo)
add_subdirectory(lego)
add_subdirectory(microsoft)
add_subdirectory(havok)
add_subdirectory(fmod)
add_subdirectory(forkparticle)
add_subdirectory(scaleform)

# Full client asset test (bulk parsing of all asset types)
add_executable(full_client_test full_client_test.cpp)
target_link_libraries(full_client_test PRIVATE lu_assets)

# Round-trip sweep (parse -> write -> byte-compare) over a real client tree
add_executable(roundtrip_sweep roundtrip_sweep.cpp)
target_link_libraries(roundtrip_sweep PRIVATE lu_assets)
