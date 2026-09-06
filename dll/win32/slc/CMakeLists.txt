
spec2def(slc.dll slc.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    slc_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/slc.def)

add_library(slc MODULE ${SOURCE})
set_module_type(slc win32dll ENTRYPOINT DllMain 12)
add_importlibs(slc msvcrt kernel32 ntdll)
add_cd_file(TARGET slc DESTINATION reactos/system32 FOR all)
