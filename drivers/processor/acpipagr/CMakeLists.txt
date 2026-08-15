# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(acpipagr MODULE acpipagr.c acpipagr.rc)
set_module_type(acpipagr kernelmodedriver)
target_link_libraries(acpipagr wdmguid)
add_importlibs(acpipagr ntoskrnl hal)
add_cd_file(TARGET acpipagr DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(acpipagr acpipagr.inf)
