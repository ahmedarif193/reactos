
spec2def(icu.dll icu.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ucnv.c
    ${CMAKE_CURRENT_BINARY_DIR}/icu.def)

add_library(icu MODULE ${SOURCE})
set_module_type(icu win32dll)
add_importlibs(icu msvcrt kernel32 ntdll)
add_cd_file(TARGET icu DESTINATION reactos/system32 FOR all)
