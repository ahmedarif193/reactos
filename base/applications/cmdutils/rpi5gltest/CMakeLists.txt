add_executable(rpi5gltest rpi5gltest.c)
set_module_type(rpi5gltest win32cui)
add_importlibs(rpi5gltest opengl32 gdi32 user32 msvcrt kernel32)
add_cd_file(TARGET rpi5gltest DESTINATION reactos/system32 FOR all)
