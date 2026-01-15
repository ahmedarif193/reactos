
spec2def(api-ms-win-core-synch-l1-2-0.dll api-ms-win-core-synch-l1-2-0.spec)

add_library(api-ms-win-core-synch-l1-2-0 MODULE
    api-ms-win-core-synch-l1-2-0.spec
    ${CMAKE_CURRENT_BINARY_DIR}/api-ms-win-core-synch-l1-2-0_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/api-ms-win-core-synch-l1-2-0.def)

set_module_type(api-ms-win-core-synch-l1-2-0 win32dll ENTRYPOINT 0)

add_importlibs(api-ms-win-core-synch-l1-2-0 kernel32 ntdll)
add_cd_file(TARGET api-ms-win-core-synch-l1-2-0 DESTINATION reactos/system32 FOR all)
