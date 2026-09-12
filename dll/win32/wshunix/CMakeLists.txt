
spec2def(wshunix.dll wshunix.spec)

list(APPEND SOURCE
    wshunix.c
    wshunix.rc
    ${CMAKE_CURRENT_BINARY_DIR}/wshunix.def)

add_library(wshunix MODULE ${SOURCE})
set_module_type(wshunix win32dll UNICODE)
add_importlibs(wshunix ws2_32 msvcrt kernel32 ntdll)
add_cd_file(TARGET wshunix DESTINATION reactos/system32 FOR all)
