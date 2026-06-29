
spec2def(api-ms-win-core-localization-l1-2-1.dll api-ms-win-core-localization-l1-2-1.spec)

add_library(api-ms-win-core-localization-l1-2-1 MODULE
    api-ms-win-core-localization-l1-2-1.spec
    ${CMAKE_CURRENT_BINARY_DIR}/api-ms-win-core-localization-l1-2-1.def)

set_module_type(api-ms-win-core-localization-l1-2-1 win32dll ENTRYPOINT 0)

add_importlibs(api-ms-win-core-localization-l1-2-1 kernel32 ntdll)
add_cd_file(TARGET api-ms-win-core-localization-l1-2-1 DESTINATION reactos/system32 FOR all)
