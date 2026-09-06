
spec2def(sndvolsso.dll sndvolsso.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    sndvolsso_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/sndvolsso.def)

add_library(sndvolsso MODULE ${SOURCE})
set_module_type(sndvolsso win32dll ENTRYPOINT DllMain 12)
add_importlibs(sndvolsso msvcrt kernel32 ntdll)
add_cd_file(TARGET sndvolsso DESTINATION reactos/system32 FOR all)
