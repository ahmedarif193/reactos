
spec2def(wbemprox.dll wbemprox.spec)

list(APPEND SOURCE
    builtin.c
    class.c
    main.c
    process.c
    qualifier.c
    query.c
    reg.c
    security.c
    service.c
    services.c
    sysrestore.c
    table.c
    wbemlocator.c)

BISON_TARGET(wql wql.y ${CMAKE_CURRENT_BINARY_DIR}/wql.tab.c)
set_source_files_properties(builtin.c PROPERTIES COMPILE_FLAGS "-include wbemprox_ros.h")

add_library(wbemprox MODULE
    ${SOURCE}
    ${BISON_wql_OUTPUTS}
    wbemprox.rc
    ${CMAKE_CURRENT_BINARY_DIR}/wbemprox.def)

set_source_files_properties(wbemprox.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wbemprox.rgs)
set_module_type(wbemprox win32dll)
target_compile_definitions(wbemprox PRIVATE __WINESRC__)
target_link_libraries(wbemprox uuid wine wine_dll_register wine_dll_canunload oldnames)
add_importlibs(wbemprox winspool version iphlpapi setupapi oleaut32 ole32 advapi32 user32 gdi32 ws2_32 rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET wbemprox DESTINATION reactos/system32/wbem FOR all)
set_wine_module(wbemprox)
