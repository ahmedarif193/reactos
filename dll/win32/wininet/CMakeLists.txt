
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)

add_definitions(
    -D_WINE
    -D_WININET_INTERNAL_)

spec2def(wininet.dll wininet.spec ADD_IMPORTLIB)
add_message_headers(UNICODE winineterror.mc)

list(APPEND SOURCE
    cookie.c
    dialogs.c
    ftp.c
    gopher.c
    http.c
    internet.c
    utility.c)

list(APPEND PCH_SKIP_SOURCE
    # Sometimes wine uses nameless structs & unions. Sometimes not... */
    netconnection.c
    urlcache.c
    ${CMAKE_CURRENT_BINARY_DIR}/wininet_stubs.c)

add_library(wininet MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/winineterror.rc
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/wininet.def)
add_dependencies(wininet winineterror)

if(MSVC)
    # Disable warning C4090: 'function': different 'const' qualifiers
    target_compile_options(wininet PRIVATE /wd4090)
endif()

set_module_type(wininet win32dll)
target_link_libraries(wininet wine ${PSEH_LIB} oldnames zlib)

add_delay_importlibs(wininet secur32 crypt32 cryptui iphlpapi dhcpcsvc)
add_importlibs(wininet mpr shlwapi shell32 user32 advapi32 ws2_32 normaliz kernel32_vista msvcrt kernel32 ntdll)
add_pch(wininet precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET wininet DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(wininet) # CORE-5743: No ARRAY_SIZE macro
