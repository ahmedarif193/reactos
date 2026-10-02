
include_directories(../../include)

list(APPEND FLTMGRTX_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    fltmgr_tx.c)

add_library(fltmgrtx_drv MODULE ${FLTMGRTX_DRV_SOURCE})
set_module_type(fltmgrtx_drv kernelmodedriver)
target_link_libraries(fltmgrtx_drv kmtest_printf ${PSEH_LIB})
add_importlibs(fltmgrtx_drv fltmgr ntoskrnl hal)
target_compile_definitions(fltmgrtx_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET fltmgrtx_drv)
