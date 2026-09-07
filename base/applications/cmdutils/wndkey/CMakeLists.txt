
add_executable(wndkey wndkey.c)
set_module_type(wndkey win32cui UNICODE)
add_importlibs(wndkey user32 msvcrt kernel32)
add_cd_file(TARGET wndkey DESTINATION reactos/system32 FOR all)
