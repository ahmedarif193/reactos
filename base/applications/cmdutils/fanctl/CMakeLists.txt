add_executable(fanctl fanctl.c)
set_module_type(fanctl win32cui UNICODE)
add_importlibs(fanctl setupapi msvcrt kernel32)
add_cd_file(TARGET fanctl DESTINATION reactos/system32 FOR all)
