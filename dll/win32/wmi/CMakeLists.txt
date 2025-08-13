
spec2def(wmi.dll wmi.spec)

list(APPEND SOURCE
    wmi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/wmi.def)

add_library(wmi MODULE ${SOURCE})
set_module_type(wmi win32dll ENTRYPOINT 0 )
add_importlibs(wmi advapi32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET wmi PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/advapi32/libadvapi32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested advapi32 archive using CMake script")
add_dependencies(wmi psdk)
add_cd_file(TARGET wmi DESTINATION reactos/system32 FOR all)
