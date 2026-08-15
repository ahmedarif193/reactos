# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(rpi5telemetry MODULE rpi5telemetry.c rpi5telemetry.rc)
set_module_type(rpi5telemetry kernelmodedriver)
target_link_libraries(rpi5telemetry wdmguid)
add_importlibs(rpi5telemetry ntoskrnl hal)
add_cd_file(TARGET rpi5telemetry DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi5telemetry rpi5telemetry.inf)
