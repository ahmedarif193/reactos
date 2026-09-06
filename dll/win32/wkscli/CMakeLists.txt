
spec2def(wkscli.dll wkscli.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    wkscli_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/wkscli.def)

add_library(wkscli MODULE ${SOURCE})
set_module_type(wkscli win32dll ENTRYPOINT DllMain 12)
add_importlibs(wkscli netapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET wkscli DESTINATION reactos/system32 FOR all)
