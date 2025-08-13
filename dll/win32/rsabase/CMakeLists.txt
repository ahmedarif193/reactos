
spec2def(rsabase.dll rsabase.spec)

list(APPEND SOURCE
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/rsabase.def)

add_library(rsabase MODULE ${SOURCE})
set_module_type(rsabase win32dll ENTRYPOINT 0 )
target_link_libraries(rsabase wine)
add_importlibs(rsabase rsaenh ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET rsabase PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/rsaenh/librsaenh.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested rsaenh archive using CMake script")
add_cd_file(TARGET rsabase DESTINATION reactos/system32 FOR all)
set_wine_module(rsabase)
