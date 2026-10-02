
include_directories(../../include)

list(APPEND WFPUSER_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    wfp_user.c)

add_library(wfpuser_drv MODULE ${WFPUSER_DRV_SOURCE})
set_module_type(wfpuser_drv kernelmodedriver)
target_link_libraries(wfpuser_drv kmtest_printf ${PSEH_LIB})
add_importlibs(wfpuser_drv fwpkclnt ndis ntoskrnl hal)
target_compile_definitions(wfpuser_drv PRIVATE KMT_STANDALONE_DRIVER NDIS630)
add_rostests_file(TARGET wfpuser_drv)
