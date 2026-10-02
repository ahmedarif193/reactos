
include_directories(../../include)

list(APPEND WFPUDP_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    wfp_udp.c)

add_library(wfpudp_drv MODULE ${WFPUDP_DRV_SOURCE})
set_module_type(wfpudp_drv kernelmodedriver)
target_link_libraries(wfpudp_drv kmtest_printf ${PSEH_LIB})
add_importlibs(wfpudp_drv fwpkclnt ndis ntoskrnl hal)
target_compile_definitions(wfpudp_drv PRIVATE KMT_STANDALONE_DRIVER NDIS630)
add_rostests_file(TARGET wfpudp_drv)
