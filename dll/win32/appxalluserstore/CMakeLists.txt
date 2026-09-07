
spec2def(appxalluserstore.dll appxalluserstore.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    appxalluserstore_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/appxalluserstore.def)

add_library(appxalluserstore MODULE ${SOURCE})
set_module_type(appxalluserstore win32dll ENTRYPOINT DllMain 12)
add_importlibs(appxalluserstore msvcrt kernel32 ntdll)
add_cd_file(TARGET appxalluserstore DESTINATION reactos/system32 FOR all)
