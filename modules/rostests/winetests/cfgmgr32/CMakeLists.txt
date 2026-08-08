add_executable(cfgmgr32_winetest
    cfgmgr32.c
    testlist.c)

set_module_type(cfgmgr32_winetest win32cui)
target_include_directories(cfgmgr32_winetest BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
target_compile_definitions(cfgmgr32_winetest PRIVATE __WINESRC__ USE_WINE_TODOS WINETEST_USE_DBG_SPRINTF)
target_link_libraries(cfgmgr32_winetest uuid)
add_importlibs(cfgmgr32_winetest cfgmgr32 advapi32 setupapi user32 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET cfgmgr32_winetest)
