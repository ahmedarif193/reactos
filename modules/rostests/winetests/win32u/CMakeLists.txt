include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine ${REACTOS_SOURCE_DIR}/dll/win32/wow64win)

generate_import_lib(win32u_winetest_imports win32u.dll win32u_imports.spec "--version=${DLL_EXPORT_VERSION}" "")
generate_import_lib(user32_winetest_imports user32.dll user32_imports.spec "--version=${DLL_EXPORT_VERSION}" "")

add_executable(win32u_winetest
    win32u.c
    testlist.c)

target_compile_definitions(win32u_winetest PRIVATE __WINESRC__)

if(MSVC)
    target_compile_options(win32u_winetest PRIVATE /wd4477)
else()
    target_compile_options(win32u_winetest PRIVATE -Wno-format)
endif()

set_module_type(win32u_winetest win32cui)
target_link_libraries(win32u_winetest win32u_winetest_imports user32_winetest_imports)
add_importlibs(win32u_winetest imm32 user32 gdi32 win32u msvcrt kernel32 ntdll)
add_rostests_file(TARGET win32u_winetest)
