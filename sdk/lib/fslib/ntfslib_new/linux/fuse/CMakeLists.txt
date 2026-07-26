find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FUSE3 QUIET IMPORTED_TARGET fuse3)
endif()

add_executable(ntfslib_fuse ntfslib_fuse.cpp)
target_link_libraries(ntfslib_fuse PRIVATE ntfslib_linux)

if(NTFSLIB_STATIC_CXX_RUNTIME AND
   CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(ntfslib_fuse PRIVATE
        -static-libstdc++
        -static-libgcc
    )
endif()

if(FUSE3_FOUND)
    target_compile_definitions(ntfslib_fuse PRIVATE NTFSLIB_HAVE_FUSE=1)
    target_link_libraries(ntfslib_fuse PRIVATE PkgConfig::FUSE3)
else()
    target_compile_definitions(ntfslib_fuse PRIVATE NTFSLIB_HAVE_FUSE=0)
    message(WARNING
        "libfuse3 development files were not found. ntfslib_fuse will "
        "support --probe/--list, but mounting requires libfuse3-dev.")
endif()
