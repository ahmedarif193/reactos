
spec2def(werkernel.sys werkernel.spec ADD_IMPORTLIB)

add_library(werkernel MODULE
    werkernel.c
    werkernel.rc
    ${CMAKE_CURRENT_BINARY_DIR}/werkernel.def)

set_module_type(werkernel kernelmodedriver)
add_importlibs(werkernel ntoskrnl hal)
add_cd_file(TARGET werkernel DESTINATION reactos/system32/drivers NO_CAB FOR all)
