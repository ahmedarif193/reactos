list(APPEND SOURCE
    usbxhci.c
    usbxhci.h
    tt.c
    osc.c)

add_library(usbxhci MODULE
    ${SOURCE}
    guid.c
    usbxhci.rc)

set_module_type(usbxhci kernelmodedriver)
# No longer needs acpi.sys - uses IOCTL_ACPI_EVAL_METHOD via lower device object
add_importlibs(usbxhci usbport usbd hal ntoskrnl)
add_pch(usbxhci usbxhci.h SOURCE)
add_cd_file(TARGET usbxhci DESTINATION reactos/system32/drivers NO_CAB FOR all)
