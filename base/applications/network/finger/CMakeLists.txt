
add_definitions(-D__USE_W32_SOCKETS)

list(APPEND SOURCE
    finger.c
    err.c
    getopt.c
    net.c
    precomp.h)

add_executable(finger ${SOURCE} finger.rc)
set_module_type(finger win32cui)
add_importlibs(finger ws2_32 msvcrt kernel32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET finger PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/ws2_32/libws2_32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested ws2_32 archive using CMake script")
add_pch(finger precomp.h SOURCE)
add_cd_file(TARGET finger DESTINATION reactos/system32 FOR all)
