
spec2def(netio.sys netio.spec ADD_IMPORTLIB)

# Including AFD is needed by the TDI helpers.
# They have been part of AFD before.

include_directories(
    BEFORE include
    ../afd/include
    ../tdihelpers/include
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

list(APPEND SOURCE
    netio.c
    nmr_stubs.c
    wfp/device.c
    wfp/engine.c
    wfp/flow.c
    wfp/packet.c
    wfp/layers.c
    fwp_ipsec.c
    iphlp.c
    ../tdihelpers/tdi.c
    ../tdihelpers/tdiconn.c
    ${CMAKE_CURRENT_BINARY_DIR}/netio.def)

add_library(netio MODULE ${SOURCE})
target_link_libraries(netio wdmsec ${PSEH_LIB})
target_compile_definitions(netio PRIVATE NDIS620)
set_module_type(netio kernelmodedriver)
add_importlibs(netio ndis ntoskrnl hal)
add_cd_file(TARGET netio DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(netio_reg.inf)
