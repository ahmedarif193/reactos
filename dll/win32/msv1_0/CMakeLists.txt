
spec2def(msv1_0.dll msv1_0.spec)

list(APPEND SOURCE
    lsa.c
    msv1_0.c
    ntlm/global.c
    ntlm/util.c
    sam.c
    user.c
    usercontext.c
    ${CMAKE_CURRENT_BINARY_DIR}/msv1_0_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msv1_0.def)

add_library(msv1_0 MODULE ${SOURCE} msv1_0.rc)
set_module_type(msv1_0 win32dll UNICODE ENTRYPOINT 0)
target_link_libraries(msv1_0 wine ${PSEH_LIB})
add_delay_importlibs(msv1_0 samsrv lsasrv)
add_importlibs(msv1_0 advapi32 kernel32 ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET msv1_0 PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/samsrv/libsamsrv_delayed.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/lsasrv/liblsasrv_delayed.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/advapi32/libadvapi32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested samsrv_delayed, lsasrv_delayed, and advapi32 archives using CMake script")
add_dependencies(msv1_0 psdk)
add_pch(msv1_0 precomp.h SOURCE)
add_cd_file(TARGET msv1_0 DESTINATION reactos/system32 FOR all)
