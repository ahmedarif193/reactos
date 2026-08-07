spec2def(iyuv_32.dll iyuv_32.spec)

list(APPEND SOURCE
    reactos/iyuv.c
    iyuv_32.rc
    ${CMAKE_CURRENT_BINARY_DIR}/iyuv_32.def)

add_library(iyuv_32 MODULE ${SOURCE})
set_module_type(iyuv_32 win32dll)
target_link_libraries(iyuv_32 wine)
add_importlibs(iyuv_32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET iyuv_32 DESTINATION reactos/system32 FOR all)
