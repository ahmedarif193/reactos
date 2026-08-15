# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(acpipmi MODULE acpipmi.c acpipmi.rc)
set_module_type(acpipmi kernelmodedriver)
add_importlibs(acpipmi ntoskrnl hal)
add_cd_file(TARGET acpipmi DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(acpipmi acpipmi.inf)
