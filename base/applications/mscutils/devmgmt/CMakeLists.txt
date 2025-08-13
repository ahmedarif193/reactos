
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/dll/devmgr)

add_executable(devmgmt devmgmt.c devmgmt.rc)
set_module_type(devmgmt win32gui UNICODE)
add_importlibs(devmgmt devmgr msvcrt kernel32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET devmgmt PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/devmgr/libdevmgr.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested devmgr archive using CMake script")
add_cd_file(TARGET devmgmt DESTINATION reactos/system32 FOR all)
