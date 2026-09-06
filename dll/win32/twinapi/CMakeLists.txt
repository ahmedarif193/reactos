
spec2def(twinapi.dll twinapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    twinapi_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/twinapi.def)

add_library(twinapi MODULE ${SOURCE})
set_module_type(twinapi win32dll ENTRYPOINT DllMain 12)
add_importlibs(twinapi msvcrt kernel32 ntdll)
add_cd_file(TARGET twinapi DESTINATION reactos/system32 FOR all)
