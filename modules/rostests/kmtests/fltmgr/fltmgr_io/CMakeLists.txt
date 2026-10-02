
include_directories(../../include)

list(APPEND FLTMGRIO_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    fltmgr_io.c)

add_library(fltmgrio_drv MODULE ${FLTMGRIO_DRV_SOURCE})
set_module_type(fltmgrio_drv kernelmodedriver)
target_link_libraries(fltmgrio_drv kmtest_printf ${PSEH_LIB})
add_importlibs(fltmgrio_drv fltmgr ntoskrnl hal)
target_compile_definitions(fltmgrio_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET fltmgrio_drv)
