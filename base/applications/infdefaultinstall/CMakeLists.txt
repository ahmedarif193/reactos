
add_executable(infdefaultinstall infdefaultinstall.c infdefaultinstall.rc)
set_module_type(infdefaultinstall win32gui UNICODE)
add_importlibs(infdefaultinstall setupapi newdev shell32 user32 msvcrt kernel32)
add_cd_file(TARGET infdefaultinstall DESTINATION reactos/system32 FOR all)
