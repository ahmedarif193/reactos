#
# PROJECT:     ReactOS CYW43455 SDIO Function Transport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     CYW43455 SDIO function transport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(cyw43455sdio MODULE cyw43455sdio.c cyw43455sdio.rc)
target_link_libraries(cyw43455sdio sdbuslib)
set_module_type(cyw43455sdio kernelmodedriver)
add_importlibs(cyw43455sdio ntoskrnl hal)
add_cd_file(TARGET cyw43455sdio DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(cyw43455sdio cyw43455sdio.inf)
