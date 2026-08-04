add_definitions(-DUSE_WINE_TODOS -DWINETEST_USE_DBGSTR_LONGLONG -D__WINESRC__)

add_executable(webservices_winetest
    channel.c
    msg.c
    proxy.c
    reader.c
    url.c
    writer.c
    testlist.c)

target_link_libraries(webservices_winetest uuid)
set_module_type(webservices_winetest win32cui)
add_importlibs(webservices_winetest webservices oleaut32 ole32 user32 rpcrt4 ws2_32 advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET webservices_winetest)
