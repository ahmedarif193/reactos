spec2def(mf3216.dll mf3216.spec)

add_library(mf3216 MODULE ${CMAKE_CURRENT_BINARY_DIR}/mf3216.def ${CMAKE_CURRENT_BINARY_DIR}/mf3216_stubs.c)
set_module_type(mf3216 win32dll ENTRYPOINT 0)
add_importlibs(mf3216 ntdll)
add_cd_file(TARGET mf3216 DESTINATION reactos/system32 FOR all)
