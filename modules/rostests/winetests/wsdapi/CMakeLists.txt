add_definitions(-DUSE_WINE_TODOS -DWINETEST_USE_DBGSTR_LONGLONG -D__WINESRC__)

list(APPEND SOURCE
    address.c
    discovery.c
    memory.c
    msgparams.c
    xml.c
    testlist.c)

add_executable(wsdapi_winetest ${SOURCE})
target_link_libraries(wsdapi_winetest uuid)
set_module_type(wsdapi_winetest win32cui)
add_importlibs(wsdapi_winetest wsdapi ole32 oleaut32 user32 ws2_32 advapi32 iphlpapi rpcrt4 msvcrt kernel32)
add_rostests_file(TARGET wsdapi_winetest)
