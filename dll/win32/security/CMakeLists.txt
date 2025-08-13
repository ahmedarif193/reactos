
add_definitions(-D__SECURITY__)
spec2def(security.dll security.spec)

list(APPEND SOURCE
    security.rc
    ${CMAKE_CURRENT_BINARY_DIR}/security.def)

add_library(security MODULE ${SOURCE})
set_module_type(security win32dll ENTRYPOINT 0 UNICODE)
add_importlibs(security secur32)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET security PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/secur32/libsecur32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested secur32 archive using CMake script")
add_cd_file(TARGET security DESTINATION reactos/system32 FOR all)
