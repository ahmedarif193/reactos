
add_executable(propsys_winetest propsys.c testlist.c)
target_compile_definitions(propsys_winetest PRIVATE
    _FORCENAMELESSUNION
    WINETEST_USE_DBG_SPRINTF
    WINETEST_USE_DBGSTR_LONGLONG
    WINETEST_USE_DBGSTR_VT)
set_module_type(propsys_winetest win32cui)
add_importlibs(propsys_winetest propsys ole32 oleaut32 msvcrt kernel32)

if(MSVC)
    add_importlibs(propsys_winetest ntdll)
endif()

add_rostests_file(TARGET propsys_winetest)
