add_executable(sdrwbench sdrwbench.c)
set_module_type(sdrwbench win32cui)
add_importlibs(sdrwbench msvcrt kernel32)
add_cd_file(TARGET sdrwbench DESTINATION reactos/system32 FOR all)
