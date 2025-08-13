
add_message_headers(UNICODE arp_msg.mc)
add_executable(arp arp.c arp.rc)
set_module_type(arp win32cui)
add_dependencies(arp arp_msg)
add_importlibs(arp iphlpapi ws2_32 shlwapi msvcrt kernel32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET arp PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/iphlpapi/libiphlpapi.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/shlwapi/libshlwapi.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested iphlpapi and shlwapi archives using CMake script")
add_cd_file(TARGET arp DESTINATION reactos/system32 FOR all)
