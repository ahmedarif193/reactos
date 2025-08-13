
add_executable(route route.c route.rc)
set_module_type(route win32cui UNICODE)
add_importlibs(route ws2_32 iphlpapi msvcrt kernel32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET route PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/ws2_32/libws2_32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested ws2_32 archive using CMake script")
add_cd_file(TARGET route DESTINATION reactos/system32 FOR all)
