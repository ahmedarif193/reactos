include_directories(
    ../include
    ${REACTOS_SOURCE_DIR}/drivers/hid/hidi2c)

list(APPEND HIDI2C_DESCRIPTOR_DRV_SOURCE
    ../kmtest_drv/kmtest_standalone.c
    Hidi2cDescriptor.c
    Hidi2cDescriptor_drv.c
    ${REACTOS_SOURCE_DIR}/drivers/hid/hidi2c/descriptor.c)

add_library(hidi2cdescriptor_drv MODULE ${HIDI2C_DESCRIPTOR_DRV_SOURCE})
set_module_type(hidi2cdescriptor_drv kernelmodedriver)
target_link_libraries(hidi2cdescriptor_drv kmtest_printf ${PSEH_LIB})
add_importlibs(hidi2cdescriptor_drv ntoskrnl hal)
target_compile_definitions(hidi2cdescriptor_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET hidi2cdescriptor_drv)

list(APPEND HIDI2C_TRANSFER_DRV_SOURCE
    ../kmtest_drv/kmtest_standalone.c
    Hidi2cTransfer.c
    Hidi2cTransfer_drv.c
    ${REACTOS_SOURCE_DIR}/drivers/hid/hidi2c/transfer.c)

add_library(hidi2ctransfer_drv MODULE ${HIDI2C_TRANSFER_DRV_SOURCE})
set_module_type(hidi2ctransfer_drv kernelmodedriver)
target_link_libraries(hidi2ctransfer_drv kmtest_printf ${PSEH_LIB})
add_importlibs(hidi2ctransfer_drv ntoskrnl hal)
target_compile_definitions(hidi2ctransfer_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET hidi2ctransfer_drv)
