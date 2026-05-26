
spec2def(sdport.sys sdport.spec ADD_IMPORTLIB)

add_library(sdport MODULE
    sdport.c
    protocol.c
    command.c
    transfer.c
    power.c
    sdport.rc
    ${CMAKE_CURRENT_BINARY_DIR}/sdport.def)

set_module_type(sdport kernelmodedriver)
target_link_libraries(sdport ${PSEH_LIB})
add_importlibs(sdport ntoskrnl hal)
add_cd_file(TARGET sdport DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(sdport_reg.inf)
