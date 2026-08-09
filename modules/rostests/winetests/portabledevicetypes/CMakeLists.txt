# Copyright 2026 Ahmed Arif
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(portabledevicetypes_winetest portabledevicetypes.c testlist.c)
set_module_type(portabledevicetypes_winetest win32cui)
target_link_libraries(portabledevicetypes_winetest uuid)
add_importlibs(portabledevicetypes_winetest portabledevicetypes ole32 oleaut32 msvcrt kernel32)
add_rostests_file(TARGET portabledevicetypes_winetest)
