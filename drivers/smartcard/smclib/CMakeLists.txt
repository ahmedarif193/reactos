
spec2def(smclib.sys smclib.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    smclib.c
    smcprot.c
    smcpriv.h)

add_library(smclib MODULE
    ${SOURCE}
    smclib.rc
    ${CMAKE_CURRENT_BINARY_DIR}/smclib.def)

set_module_type(smclib kernelmodedriver ENTRYPOINT 0)
add_importlibs(smclib ntoskrnl hal)
add_cd_file(TARGET smclib DESTINATION reactos/system32/drivers NO_CAB FOR all)
