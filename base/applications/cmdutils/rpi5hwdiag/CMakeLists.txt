# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(rpi5hwdiag rpi5hwdiag.c rpi5hwdiag.rc)
set_module_type(rpi5hwdiag win32cui UNICODE)
target_link_libraries(rpi5hwdiag uuid)
add_importlibs(rpi5hwdiag setupapi cfgmgr32 ole32 oleaut32 propsys opengl32 gdi32 user32 msvcrt kernel32)
add_cd_file(TARGET rpi5hwdiag DESTINATION reactos/system32 FOR all)
