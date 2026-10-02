
include_directories(../../include)

list(APPEND WFPTCP_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    wfp_tcp.c)

add_library(wfptcp_drv MODULE ${WFPTCP_DRV_SOURCE})
set_module_type(wfptcp_drv kernelmodedriver)
target_link_libraries(wfptcp_drv kmtest_printf ${PSEH_LIB})
add_importlibs(wfptcp_drv fwpkclnt ndis ntoskrnl hal)
target_compile_definitions(wfptcp_drv PRIVATE KMT_STANDALONE_DRIVER NDIS630)
add_rostests_file(TARGET wfptcp_drv)
