
spec2def(cng.sys cng.spec ADD_IMPORTLIB)

add_library(cng MODULE
    cng.c
    cng.rc
    ${CMAKE_CURRENT_BINARY_DIR}/cng.def)

set_module_type(cng kernelmodedriver)
add_importlibs(cng ntoskrnl hal)
add_dependencies(cng ksecdd)
add_cd_file(TARGET cng DESTINATION reactos/system32/drivers NO_CAB FOR all)
