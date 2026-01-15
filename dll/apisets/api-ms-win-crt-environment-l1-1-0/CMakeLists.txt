
spec2def(api-ms-win-crt-environment-l1-1-0.dll api-ms-win-crt-environment-l1-1-0.spec)

add_library(api-ms-win-crt-environment-l1-1-0 MODULE
    api-ms-win-crt-environment-l1-1-0.spec
    ${CMAKE_CURRENT_BINARY_DIR}/api-ms-win-crt-environment-l1-1-0.def)

set_module_type(api-ms-win-crt-environment-l1-1-0 win32dll ENTRYPOINT 0)

add_importlibs(api-ms-win-crt-environment-l1-1-0 ucrtbase ntdll)
add_cd_file(TARGET api-ms-win-crt-environment-l1-1-0 DESTINATION reactos/system32 FOR all)
