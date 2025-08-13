
add_executable(schtasks schtasks.c)
target_link_libraries(schtasks wine)
set_module_type(schtasks win32cui UNICODE)
add_importlibs(schtasks oleaut32 ole32 msvcrt kernel32 ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET schtasks PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/oleaut32/liboleaut32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/ole32/libole32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested oleaut32 and ole32 archives using CMake script")
add_cd_file(TARGET schtasks DESTINATION reactos/system32 FOR all)
