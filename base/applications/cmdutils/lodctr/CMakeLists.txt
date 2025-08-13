
add_executable(lodctr lodctr_main.c)
set_module_type(lodctr win32cui UNICODE)
add_importlibs(lodctr loadperf msvcrt kernel32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET lodctr PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/loadperf/libloadperf.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested loadperf archive using CMake script")
add_cd_file(TARGET lodctr DESTINATION reactos/system32 FOR all)
