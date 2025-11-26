add_library(usbdlib STATIC
    usbd_querycap.c)

target_include_directories(usbdlib PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include
    ${REACTOS_SOURCE_DIR}/sdk/include/ddk
    ${REACTOS_SOURCE_DIR}/sdk/include/psdk)

