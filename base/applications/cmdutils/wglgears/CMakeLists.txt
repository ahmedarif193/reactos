# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(glgears wglgears.c glad_compat.c)
target_include_directories(glgears PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(glgears PRIVATE _USE_MATH_DEFINES)
set_module_type(glgears win32cui)
add_importlibs(glgears opengl32 gdi32 user32 winmm msvcrt kernel32)
add_cd_file(TARGET glgears DESTINATION reactos/system32 FOR all)

add_executable(wglgears_runner wglgears_runner.c)
set_module_type(wglgears_runner win32cui)
add_importlibs(wglgears_runner user32 msvcrt kernel32)
add_cd_file(TARGET wglgears_runner DESTINATION reactos/system32 FOR all)
