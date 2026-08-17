# Copyright 2026 Ahmed Arif <arif193@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(rpi5gditest rpi5gditest.c)
set_module_type(rpi5gditest win32cui)
add_importlibs(rpi5gditest gdi32 user32 msvcrt kernel32)
add_cd_file(TARGET rpi5gditest DESTINATION reactos/system32 FOR all)
