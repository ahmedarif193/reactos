
#
# PROJECT:     ReactOS RP1 Bus Driver
# LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
# FILE:        drivers/bus/rp1/CMakeLists.txt
# PURPOSE:     RP1 southbridge bus driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    interrupt.c
    rp1.c
    rp1.h)

add_library(rp1 MODULE ${SOURCE} rp1.rc)
set_module_type(rp1 kernelmodedriver)
add_importlibs(rp1 ntoskrnl hal)
add_cd_file(TARGET rp1 DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(rp1 rp1.inf)
