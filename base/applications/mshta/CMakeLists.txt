
add_executable(mshta main.c mshta.rc)
target_link_libraries(mshta)
set_module_type(mshta win32gui)
add_importlibs(mshta msvcrt mshtml kernel32 ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET mshta PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/mshtml/libmshtml.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested mshtml archive using CMake script")
add_cd_file(TARGET mshta DESTINATION reactos/system32 FOR all)
