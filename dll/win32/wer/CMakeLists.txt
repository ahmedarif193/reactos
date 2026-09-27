
spec2def(wer.dll wer.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    wer.c
    ${CMAKE_CURRENT_BINARY_DIR}/wer.def)

add_library(wer MODULE ${SOURCE})
set_module_type(wer win32dll)
add_importlibs(wer msvcrt kernel32 ntdll)
add_cd_file(TARGET wer DESTINATION reactos/system32 FOR all)
