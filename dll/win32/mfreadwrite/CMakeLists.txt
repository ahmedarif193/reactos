
spec2def(mfreadwrite.dll mfreadwrite.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/mfreadwrite_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/mfreadwrite.def)

add_library(mfreadwrite MODULE ${SOURCE})
set_module_type(mfreadwrite win32dll)
target_link_libraries(mfreadwrite wine)
add_importlibs(mfreadwrite msvcrt kernel32 ntdll)
add_cd_file(TARGET mfreadwrite DESTINATION reactos/system32 FOR all)
