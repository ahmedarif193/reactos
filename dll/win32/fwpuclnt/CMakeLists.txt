spec2def(fwpuclnt.dll fwpuclnt.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    fwpuclnt.c
    ${CMAKE_CURRENT_BINARY_DIR}/fwpuclnt_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/fwpuclnt.def)

add_library(fwpuclnt MODULE ${SOURCE})
set_module_type(fwpuclnt win32dll ENTRYPOINT 0)
add_importlibs(fwpuclnt advapi32 kernel32 ntdll)
add_cd_file(TARGET fwpuclnt DESTINATION reactos/system32 FOR all)
