# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
spec2def(dwmprof.dll dwmprof.spec ADD_IMPORTLIB)
add_library(dwmprof MODULE dwmprof.c ${CMAKE_CURRENT_BINARY_DIR}/dwmprof.def)
set_module_type(dwmprof win32dll UNICODE)
add_importlibs(dwmprof user32 msvcrt kernel32)
add_cd_file(TARGET dwmprof DESTINATION reactos/system32 FOR all)
