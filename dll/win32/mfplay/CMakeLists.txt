spec2def(mfplay.dll mfplay.spec ADD_IMPORTLIB)

add_library(mfplay MODULE player.c ${CMAKE_CURRENT_BINARY_DIR}/mfplay.def ${CMAKE_CURRENT_BINARY_DIR}/mfplay_stubs.c)
set_module_type(mfplay win32dll)
target_compile_definitions(mfplay PRIVATE __WINESRC__)
target_link_libraries(mfplay mfuuid strmiids uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(mfplay mfplat mf ole32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET mfplay DESTINATION reactos/system32 FOR all)
set_wine_module(mfplay)
