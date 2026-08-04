spec2def(nsi.dll nsi.spec ADD_IMPORTLIB)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/tdilib)

add_library(nsi MODULE
    nsi.c
    nsi_reactos.c
    ${CMAKE_CURRENT_BINARY_DIR}/nsi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/nsi.def)

set_module_type(nsi win32dll)
target_link_libraries(nsi iphlpapi_native uuid wine tdilib)
add_importlibs(nsi advapi32 ws2_32 msvcrt kernel32 ntdll)
add_cd_file(TARGET nsi DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(nsi)
