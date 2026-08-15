# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(usbdwc2 MODULE
    usbdwc2.c
    guid.c
    usbdwc2.rc)

set_module_type(usbdwc2 kernelmodedriver)
add_importlibs(usbdwc2 usbport usbd hal ntoskrnl)
add_pch(usbdwc2 usbdwc2.h usbdwc2.c)
add_cd_file(TARGET usbdwc2 DESTINATION reactos/system32/drivers NO_CAB FOR all)
