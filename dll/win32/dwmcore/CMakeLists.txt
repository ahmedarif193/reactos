
spec2def(dwmcore.dll dwmcore.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dwmcore_main.c)

add_library(dwmcore MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dwmcore.def
    ${CMAKE_CURRENT_BINARY_DIR}/dwmcore_stubs.c)

set_module_type(dwmcore win32dll UNICODE ENTRYPOINT DllMain 12)
target_link_libraries(dwmcore uuid wine)
add_importlibs(dwmcore kernel32 ntdll)
add_cd_file(TARGET dwmcore DESTINATION reactos/system32 FOR all)
