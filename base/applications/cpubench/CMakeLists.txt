
add_executable(cpubench cpubench.c)
set_module_type(cpubench win32cui)
add_importlibs(cpubench msvcrt kernel32)
add_cd_file(TARGET cpubench DESTINATION reactos/system32 FOR all)
