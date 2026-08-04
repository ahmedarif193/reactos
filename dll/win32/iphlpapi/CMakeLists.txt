
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/wine
    ${REACTOS_SOURCE_DIR}/dll/win32/dhcpcsvc/include
    ${REACTOS_SOURCE_DIR}/sdk/lib/tdilib)

spec2def(iphlpapi.dll iphlpapi.spec ADD_IMPORTLIB)

add_definitions(
    -DGetAdaptersAddressesV2
    -D_CRT_NO_POSIX_ERROR_CODES)

list(APPEND NATIVE_SOURCE
    address.c
    dhcp_reactos.c
    ifenum_reactos.c
    ipstats_reactos.c
    resinfo_reactos.c
    route_reactos.c
    iphlpapi_private.h)

add_library(iphlpapi_native STATIC ${NATIVE_SOURCE})
target_compile_definitions(iphlpapi_native PRIVATE UNICODE _UNICODE _DLL __USE_CRTIMP _CRT_DECLARE_NONSTDC_NAMES=1 _CRT_NONSTDC_NO_DEPRECATE)
target_link_libraries(iphlpapi_native wine tdilib)
add_pch(iphlpapi_native iphlpapi_private.h NATIVE_SOURCE)

list(APPEND SOURCE
    icmp.c
    iphlpapi_reactos.c
    iphlpapi_main.c
    media.c
    netio_reactos.c
    registry.c
    iphlpapi_private.h
    ${CMAKE_CURRENT_BINARY_DIR}/iphlpapi_stubs.c)

add_library(iphlpapi MODULE
    ${SOURCE}
    iphlpapi.rc
    iphlpapi_fakestubs.c
    iphlpapi.spec
    ${CMAKE_CURRENT_BINARY_DIR}/iphlpapi.def)

set_module_type(iphlpapi win32dll UNICODE)
target_link_libraries(iphlpapi iphlpapi_native wine tdilib)
add_importlibs(iphlpapi dhcpcsvc advapi32 psapi ws2_32 msvcrt kernel32 ntdll)
add_pch(iphlpapi iphlpapi_private.h SOURCE)
add_cd_file(TARGET iphlpapi DESTINATION reactos/system32 FOR all)
