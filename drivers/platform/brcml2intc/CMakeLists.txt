# PROJECT:     ReactOS Broadcom L2 Interrupt Controller Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(brcml2intc MODULE brcml2intc.c brcml2intc.rc)
set_module_type(brcml2intc kmdfdriver)
target_link_libraries(brcml2intc msgpioclxstub ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(brcml2intc ntoskrnl hal)
add_cd_file(TARGET brcml2intc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(brcml2intc brcml2intc.inf)
set_target_properties(brcml2intc PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 5")
