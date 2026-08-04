
add_executable(write write.c write.rc rsrc.rc)
set_module_type(write win32gui)
add_importlibs(write user32 gdi32 shell32 msvcrt kernel32)
add_cd_file(TARGET write DESTINATION reactos/system32 FOR all)
