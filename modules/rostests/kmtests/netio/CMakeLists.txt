
include_directories(../include)

list(APPEND NETIO_TEST_DRV_SOURCE
    ../kmtest_drv/kmtest_standalone.c
    Netio_drv.c)

add_library(netio_drv MODULE ${NETIO_TEST_DRV_SOURCE})
set_module_type(netio_drv kernelmodedriver)
target_link_libraries(netio_drv kmtest_printf ${PSEH_LIB})
add_importlibs(netio_drv ntoskrnl hal netio)
target_compile_definitions(netio_drv PRIVATE KMT_STANDALONE_DRIVER)
#add_pch(netio_drv ../include/kmt_test.h)
add_rostests_file(TARGET netio_drv)
