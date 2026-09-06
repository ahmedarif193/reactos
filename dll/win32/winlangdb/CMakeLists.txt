
spec2def(winlangdb.dll winlangdb.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    winlangdb_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/winlangdb.def)

add_library(winlangdb MODULE ${SOURCE})
set_module_type(winlangdb win32dll ENTRYPOINT DllMain 12)
add_importlibs(winlangdb msvcrt kernel32 ntdll)
add_cd_file(TARGET winlangdb DESTINATION reactos/system32 FOR all)
