
include_directories(${SHIMLIB_DIR})

remove_definitions(-D_WIN32_WINNT=0x502 -DWINVER=0x502)
add_definitions(-D_WIN32_WINNT=0x601 -DWINVER=0x601)
#add_definitions(-DSDBAPI_DEBUG_ALLOC)

spec2def(apphelp.dll apphelp.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    apphelp.c
    dbgheap.c
    hsdb.c
    layer.c
    sdbapi.c
    sdbfileattr.c
    sdbread.c
    sdbstringtable.c
    sdbwrite.c
    shimeng.c
    apphelp.spec
    ${CMAKE_CURRENT_BINARY_DIR}/apphelp_stubs.c)

list(APPEND HEADERS
    apphelp.h
    sdbpapi.h
    sdbstringtable.h
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/appcompat/sdbtagid.h
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/appcompat/sdbtypes.h
    sdbwrite.h
    shimeng.h)

add_library(apphelp MODULE
    ${SOURCE}
    ${HEADERS}
    apphelp.rc
    ${CMAKE_CURRENT_BINARY_DIR}/apphelp.def)

set_module_type(apphelp win32dll UNICODE ENTRYPOINT DllMain 12)
add_delay_importlibs(apphelp version)
add_importlibs(apphelp kernel32 ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET apphelp PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/version/libversion_delayed.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested version_delayed archive using CMake script")
add_cd_file(TARGET apphelp DESTINATION reactos/system32 FOR all)
