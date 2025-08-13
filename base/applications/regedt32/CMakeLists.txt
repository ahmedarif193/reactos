
add_rc_deps(resource.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/regedt32.ico)
add_executable(regedt32 regedt32.c resource.rc)
set_module_type(regedt32 win32gui UNICODE)
add_importlibs(regedt32 shell32 shlwapi msvcrt kernel32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET regedt32 PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/shlwapi/libshlwapi.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested shlwapi archive using CMake script")
add_cd_file(TARGET regedt32 DESTINATION reactos/system32 FOR all)
