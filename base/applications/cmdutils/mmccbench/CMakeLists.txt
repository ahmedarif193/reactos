# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
add_executable(mmccbench mmccbench.c)
set_module_type(mmccbench win32cui)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mmccbench PRIVATE -O2)
endif()
add_importlibs(mmccbench msvcrt kernel32)
add_cd_file(TARGET mmccbench DESTINATION reactos/system32 FOR all)
