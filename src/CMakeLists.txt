add_library(lu_assets STATIC)

target_include_directories(lu_assets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${stb_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/lu_assets>
)

target_link_libraries(lu_assets PUBLIC
    ZLIB::ZLIB
    SQLite::SQLite3
    pugixml::pugixml
)

add_subdirectory(common)
add_subdirectory(netdevil)
add_subdirectory(gamebryo)
add_subdirectory(havok)
add_subdirectory(lego)
add_subdirectory(microsoft)
add_subdirectory(forkparticle)
add_subdirectory(fmod)
add_subdirectory(scaleform)
