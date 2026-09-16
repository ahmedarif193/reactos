# PROJECT:     ReactOS HID Stack
# SPDX-License-Identifier: MIT
# PURPOSE:     Xbox USB gamepad HID minidriver build
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

add_library(xboxhid MODULE xboxhid.c xboxhid.rc)
set_module_type(xboxhid kernelmodedriver)
target_link_libraries(xboxhid gip)
add_importlibs(xboxhid hidclass ntoskrnl usbd hal)
add_cd_file(TARGET xboxhid DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(xboxhid xboxhid.inf)
