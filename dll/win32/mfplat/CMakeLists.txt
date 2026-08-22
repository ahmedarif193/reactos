spec2def(mfplat.dll mfplat.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    buffer.c
    main.c
    mediatype.c
    queue.c
    sample.c)

add_library(mfplat MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/mfplat.def ${CMAKE_CURRENT_BINARY_DIR}/mfplat_stubs.c)
set_module_type(mfplat win32dll)
target_compile_definitions(mfplat PRIVATE __DO_ALIGN_DEFINES __WINESRC__)
target_link_libraries(mfplat dmoguids mfuuid uuid wine)
add_importlibs(mfplat advapi32 ole32 propsys rtworkq kernelbase msvcrt kernel32 ntdll)
add_delay_importlibs(mfplat bcrypt)
add_cd_file(TARGET mfplat DESTINATION reactos/system32 FOR all)
set_wine_module(mfplat)
