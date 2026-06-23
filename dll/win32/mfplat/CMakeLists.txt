
spec2def(mfplat.dll mfplat.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/mfplat_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/mfplat.def)

add_library(mfplat MODULE ${SOURCE})
set_module_type(mfplat win32dll)
target_link_libraries(mfplat wine)
add_importlibs(mfplat msvcrt kernel32 ntdll)
add_cd_file(TARGET mfplat DESTINATION reactos/system32 FOR all)
