
include_directories(../../include)

list(APPEND FLTMGRSCAN_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    fltmgr_scan.c)

add_library(fltmgrscan_drv MODULE ${FLTMGRSCAN_DRV_SOURCE})
set_module_type(fltmgrscan_drv kernelmodedriver)
target_link_libraries(fltmgrscan_drv kmtest_printf ${PSEH_LIB})
add_importlibs(fltmgrscan_drv fltmgr ntoskrnl hal)
target_compile_definitions(fltmgrscan_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET fltmgrscan_drv)
