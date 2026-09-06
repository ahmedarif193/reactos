
spec2def(dui70.dll dui70.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dui70_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/dui70.def)

add_library(dui70 MODULE ${SOURCE})
set_module_type(dui70 win32dll ENTRYPOINT DllMain 12)
add_importlibs(dui70 msvcrt kernel32 ntdll)
add_cd_file(TARGET dui70 DESTINATION reactos/system32 FOR all)
