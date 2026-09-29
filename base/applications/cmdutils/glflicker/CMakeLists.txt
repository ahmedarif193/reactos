add_executable(glflicker glflicker.c)
set_module_type(glflicker win32cui)
add_importlibs(glflicker user32 gdi32 msvcrt kernel32)
add_cd_file(TARGET glflicker DESTINATION reactos/system32 FOR all)
