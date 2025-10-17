
list(APPEND SOURCE
    WinStationGetProcessSid.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(winsta_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(winsta_apitest ${PSEH_LIB})
set_module_type(winsta_apitest win32cui)
add_importlibs(winsta_apitest winsta advapi32 ntdll msvcrt kernel32)
add_pch(winsta_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET winsta_apitest)
