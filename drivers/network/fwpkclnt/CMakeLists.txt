
spec2def(fwpkclnt.sys fwpkclnt.spec ADD_IMPORTLIB)

add_library(fwpkclnt MODULE
    fwpkclnt.c
    fwpkclnt.rc
    ${CMAKE_CURRENT_BINARY_DIR}/fwpkclnt_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/fwpkclnt.def)

set_module_type(fwpkclnt kernelmodedriver)
add_importlibs(fwpkclnt ntoskrnl hal)
add_dependencies(fwpkclnt netio)
add_cd_file(TARGET fwpkclnt DESTINATION reactos/system32/drivers NO_CAB FOR all)
