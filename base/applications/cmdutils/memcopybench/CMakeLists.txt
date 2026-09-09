# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
add_executable(memcopybench memcopybench.c)
set_module_type(memcopybench win32cui)
# Optimize the harness, even in the DEBUG OS build. Copy variants use explicit
# assembly or runtime DLL exports, so compiler memcpy substitution cannot hide
# the implementation being measured.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(memcopybench PRIVATE -O2 -fno-builtin)
endif()
target_link_libraries(memcopybench pseh)
add_importlibs(memcopybench msvcrt kernel32)
add_cd_file(TARGET memcopybench DESTINATION reactos/system32 FOR all)
# Optional direct comparison of external Mesa source snapshots. No dependency
# on Mesa is introduced for ordinary builds of this diagnostic.
set(MEMCOPYBENCH_MESA_BEFORE "" CACHE FILEPATH "Previous v3d_cpu_tiling.h for copy measurements")
set(MEMCOPYBENCH_MESA_AFTER "" CACHE FILEPATH "Current v3d_cpu_tiling.h for copy measurements")
if(MEMCOPYBENCH_MESA_BEFORE AND MEMCOPYBENCH_MESA_AFTER)
    target_compile_definitions(memcopybench PRIVATE
        MEMCOPYBENCH_MESA_BEFORE="${MEMCOPYBENCH_MESA_BEFORE}"
        MEMCOPYBENCH_MESA_AFTER="${MEMCOPYBENCH_MESA_AFTER}")
endif()
