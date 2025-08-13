project(appcompat)
include_directories(${REACTOS_SOURCE_DIR}/dll/appcompat/apphelp)
add_executable(sdbinst sdbinst.c)

set_module_type(sdbinst win32cui UNICODE)
add_importlibs(sdbinst shlwapi advapi32 apphelp ole32 msvcrt kernel32 ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET sdbinst PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/shlwapi/libshlwapi.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested shlwapi archive using CMake script")
add_cd_file(TARGET sdbinst DESTINATION reactos/system32 FOR all)
