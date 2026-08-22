spec2def(mfreadwrite.dll mfreadwrite.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    reader.c
    writer.c)

add_library(mfreadwrite MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/mfreadwrite.def ${CMAKE_CURRENT_BINARY_DIR}/mfreadwrite_stubs.c)
add_idl_reg_scripts(mfreadwrite registry mf_classes.idl)
set_module_type(mfreadwrite win32dll)
target_compile_definitions(mfreadwrite PRIVATE __WINESRC__)
target_link_libraries(mfreadwrite mfuuid uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(mfreadwrite mfplat ole32 kernelbase msvcrt kernel32 ntdll)
add_delay_importlibs(mfreadwrite mf)
add_cd_file(TARGET mfreadwrite DESTINATION reactos/system32 FOR all)
set_wine_module(mfreadwrite)
