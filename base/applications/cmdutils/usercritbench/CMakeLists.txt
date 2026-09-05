add_executable(usercritbench usercritbench.c)
set_module_type(usercritbench win32cui UNICODE)
add_importlibs(usercritbench user32 msvcrt kernel32)
add_cd_file(TARGET usercritbench DESTINATION reactos/system32 FOR all)
