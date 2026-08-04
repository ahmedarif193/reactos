include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

add_executable(schtasks_winetest
    schtasks.c
    testlist.c)
set_module_type(schtasks_winetest win32cui)
target_link_libraries(schtasks_winetest wine)
add_importlibs(schtasks_winetest advapi32 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET schtasks_winetest)
