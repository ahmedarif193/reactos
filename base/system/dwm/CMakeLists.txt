
add_executable(dwm dwm.c)
set_module_type(dwm win32gui UNICODE)
add_importlibs(dwm win32u user32 gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dwm DESTINATION reactos/system32 FOR all)
