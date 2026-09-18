# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

add_rc_deps(dwmsettings.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/../control11/res/cat_appearance.ico)
add_executable(dwmsettings
    dwmsettings.c
    ../../system/dwm/settings.c
    dwmsettings.rc)
target_include_directories(dwmsettings PRIVATE ${REACTOS_SOURCE_DIR}/base/system/dwm)
set_module_type(dwmsettings win32gui UNICODE)
add_importlibs(dwmsettings uxtheme advapi32 user32 gdi32 msvcrt kernel32)
add_cd_file(TARGET dwmsettings DESTINATION reactos/system32 FOR all)
