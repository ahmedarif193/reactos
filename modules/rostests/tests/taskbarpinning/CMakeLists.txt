# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Ahmed Arif
add_executable(taskbarpinning taskbarpinning.c)
set_module_type(taskbarpinning win32cui ENTRYPOINT mainCRTStartup)
target_link_libraries(taskbarpinning uuid)
add_importlibs(taskbarpinning advapi32 shell32 shlwapi ole32 user32 msvcrt kernel32)
add_rostests_file(TARGET taskbarpinning)
