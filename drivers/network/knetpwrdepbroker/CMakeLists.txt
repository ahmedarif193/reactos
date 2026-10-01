
spec2def(knetpwrdepbroker.sys knetpwrdepbroker.spec ADD_IMPORTLIB)

add_library(knetpwrdepbroker MODULE
    knetpwrdepbroker.c
    knetpwrdepbroker.rc
    ${CMAKE_CURRENT_BINARY_DIR}/knetpwrdepbroker.def)

set_module_type(knetpwrdepbroker kernelmodedriver)
add_importlibs(knetpwrdepbroker ntoskrnl hal)
add_cd_file(TARGET knetpwrdepbroker DESTINATION reactos/system32/drivers NO_CAB FOR all)
