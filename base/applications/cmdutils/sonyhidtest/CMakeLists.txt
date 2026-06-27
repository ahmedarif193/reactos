add_executable(sonyhidtest sonyhidtest.c)
set_module_type(sonyhidtest win32cui UNICODE)
add_importlibs(sonyhidtest setupapi hid msvcrt kernel32)
add_cd_file(TARGET sonyhidtest DESTINATION reactos/system32 FOR all)
