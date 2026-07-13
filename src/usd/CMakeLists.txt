# lu_assets_usd — Universal Scene Description translation layer.
#
# A separate library (not folded into lu_assets) so the heavy OpenUSD
# dependency stays opt-in. Built only when LU_ASSETS_BUILD_USD is ON.
#
# OpenUSD resolution: prefer a system install (fast; find_package(pxr)),
# fall back to a FetchContent source build only when none is installed
# (e.g. a clean CI image). OpenUSD is very large to compile from source,
# so the system path is strongly preferred where available.

# The system OpenUSD config transitively depends on MaterialX, whose imported
# targets reference X11::X11 (via MaterialXRenderHw). Provide it up front so the
# transitive find_dependency inside pxrConfig.cmake resolves. Harmless when USD
# is built from source (X11 simply may or may not be found).
find_package(X11 QUIET)
# OpenSubdiv's imported targets (pulled in transitively by pxr) list
# Threads::Threads in their link interface; provide it so those targets resolve.
find_package(Threads QUIET)

find_package(pxr QUIET)

if(pxr_FOUND)
    message(STATUS "lu_assets_usd: using system OpenUSD (${PXR_VERSION})")
    # find_package(pxr) sets PXR_LIBRARIES to the correct link targets for the
    # install, whether it was built as split per-schema libraries or as a single
    # monolithic library (usd_m / libusd_ms.so). Using it directly avoids
    # hardcoding names that only exist in one of those layouts — the split names
    # (usd, usdGeom, ...) are INTERFACE targets in a monolithic install and
    # carry no symbols, which fails at link time.
    set(LU_USD_LIBS ${PXR_LIBRARIES})

    # System OpenUSD configs propagate optional-plugin dependencies into the
    # link interface of transitive imported targets — plugins that are loaded at
    # runtime, not required to link a program that only *writes* USD. On this
    # kind of packaging several of those deps are absent (OpenSubdiv's GPU
    # targets reference a CUDA runtime under /opt/cuda; Alembic references
    # hdf5-shared SONAMEs that aren't installed), so naively consuming the pxr
    # interface fails to link. Rather than chase each optional plugin, walk every
    # imported target reachable from the pxr libraries and scrub INTERFACE link
    # entries that can't be resolved on this machine:
    #   * absolute paths that don't exist, and
    #   * plain lib names / -l flags with no findable library.
    # This keeps the genuinely-required deps (TBB, the USD core) and drops the
    # optional accelerators/back-ends mesh authoring never touches.
    function(_lu_scrub_target _target _seen_var)
        if(NOT TARGET ${_target})
            return()
        endif()
        # Only imported targets carry the leaked absolute/SONAME deps and can be
        # safely rewritten. Skip alias targets (get_target_property on an ALIAS
        # errors) and non-imported first-party targets.
        get_target_property(_aliased ${_target} ALIASED_TARGET)
        if(_aliased)
            return()
        endif()
        get_target_property(_imported ${_target} IMPORTED)
        if(NOT _imported)
            return()
        endif()

        list(FIND ${_seen_var} ${_target} _idx)
        if(NOT _idx EQUAL -1)
            return()
        endif()
        list(APPEND ${_seen_var} ${_target})
        set(${_seen_var} "${${_seen_var}}" PARENT_SCOPE)

        get_target_property(_libs ${_target} INTERFACE_LINK_LIBRARIES)
        if(_libs)
            set(_kept)
            foreach(_lib IN LISTS _libs)
                set(_drop FALSE)
                if(IS_ABSOLUTE "${_lib}" AND NOT EXISTS "${_lib}")
                    set(_drop TRUE)
                elseif(_lib MATCHES "^(hdf5|hdf5_cpp|hdf5_hl|hdf5_hl_cpp)(-shared)?$"
                       OR _lib MATCHES "^-l(hdf5|hdf5_cpp|hdf5_hl|hdf5_hl_cpp)")
                    # Alembic/HDF5 back-end: not needed to author USD, and the
                    # SONAME variants it names aren't installed here.
                    set(_drop TRUE)
                endif()
                if(_drop)
                    message(STATUS "lu_assets_usd: scrubbing unresolved dep '${_lib}' from ${_target}")
                else()
                    list(APPEND _kept "${_lib}")
                    # Recurse into imported target dependencies to reach nested
                    # offenders (e.g. Alembic -> hdf5-shared). Non-target entries
                    # (raw paths, -l flags) are handled in place above.
                    if(TARGET ${_lib})
                        _lu_scrub_target(${_lib} ${_seen_var})
                        set(${_seen_var} "${${_seen_var}}" PARENT_SCOPE)
                    endif()
                endif()
            endforeach()
            set_target_properties(${_target} PROPERTIES INTERFACE_LINK_LIBRARIES "${_kept}")
        endif()
    endfunction()
    set(_lu_scrub_seen)
    foreach(_root IN LISTS LU_USD_LIBS)
        if(TARGET ${_root})
            _lu_scrub_target(${_root} _lu_scrub_seen)
        endif()
    endforeach()
else()
    message(STATUS "lu_assets_usd: no system OpenUSD found — building from source via FetchContent (this is slow)")
    include(FetchContent)
    # Minimal monolithic build: no Python, no imaging, no tools — we only
    # need the core scene-description + usdGeom/usdShade/usdSkel schemas.
    set(PXR_ENABLE_PYTHON_SUPPORT OFF CACHE BOOL "" FORCE)
    set(PXR_BUILD_IMAGING OFF CACHE BOOL "" FORCE)
    set(PXR_BUILD_USD_IMAGING OFF CACHE BOOL "" FORCE)
    set(PXR_BUILD_USDVIEW OFF CACHE BOOL "" FORCE)
    set(PXR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(PXR_BUILD_TUTORIALS OFF CACHE BOOL "" FORCE)
    set(PXR_BUILD_MONOLITHIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(openusd
        GIT_REPOSITORY https://github.com/PixarAnimationStudios/OpenUSD.git
        GIT_TAG v25.05
    )
    FetchContent_MakeAvailable(openusd)
    set(LU_USD_LIBS usd_ms)
endif()

add_library(lu_assets_usd
    ${CMAKE_CURRENT_SOURCE_DIR}/nif_to_usd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/usd_to_nif.cpp
)
# src/ (parent of src/usd) as the include root so headers resolve as
# "usd/nif_to_usd.h", matching how the core library's own headers are included
# (e.g. "gamebryo/nif/nif_types.h").
target_include_directories(lu_assets_usd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lu_assets_usd PUBLIC lu_assets ${LU_USD_LIBS})
add_library(lu_assets::usd ALIAS lu_assets_usd)
