
spec2def(profapi.dll profapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    profapi_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/profapi.def)

add_library(profapi MODULE ${SOURCE})
set_module_type(profapi win32dll ENTRYPOINT DllMain 12)
add_importlibs(profapi msvcrt kernel32 ntdll)
add_cd_file(TARGET profapi DESTINATION reactos/system32 FOR all)
