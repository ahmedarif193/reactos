
list(APPEND SOURCE
    GetExtendedTcpTable.c
    GetExtendedUdpTable.c
    GetInterfaceName.c
    GetNetworkParams.c
    GetOwnerModuleFromTcpEntry.c
    GetOwnerModuleFromUdpEntry.c
    icmp.c
    SendARP.c
    testlist.c)

add_executable(iphlpapi_apitest ${SOURCE})
target_link_libraries(iphlpapi_apitest wine ${PSEH_LIB})
set_module_type(iphlpapi_apitest win32cui)
add_importlibs(iphlpapi_apitest iphlpapi ws2_32 advapi32 msvcrt kernel32 ntdll)
if(NOT MSVC)
    set_source_files_properties(GetNetworkParams.c PROPERTIES COMPILE_OPTIONS "-Wno-address")
endif()
add_rostests_file(TARGET iphlpapi_apitest)
