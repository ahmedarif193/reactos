add_executable(diskbench diskbench.c)
set_module_type(diskbench win32cui)
add_importlibs(diskbench msvcrt kernel32)
add_cd_file(TARGET diskbench DESTINATION reactos/system32 FOR all)
