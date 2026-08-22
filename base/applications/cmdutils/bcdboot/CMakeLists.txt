#
# PROJECT:     ReactOS Boot Configuration Data boot file creation tool
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Builds and packages the BCDBoot compatibility component
# COPYRIGHT:   Copyright 2026 Ahmed Arif
#

add_executable(bcdboot bcdboot.c bcdboot.rc)
set_module_type(bcdboot win32cui UNICODE)
add_importlibs(bcdboot msvcrt kernel32)
add_cd_file(TARGET bcdboot DESTINATION reactos/system32 FOR all)
