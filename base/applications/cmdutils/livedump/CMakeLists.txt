# PROJECT:     ReactOS live kernel dump utility
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(livedump livedump.c)
set_module_type(livedump win32cui UNICODE)
add_importlibs(livedump msvcrt kernel32 ntdll)
add_cd_file(TARGET livedump DESTINATION reactos/system32 FOR all)
