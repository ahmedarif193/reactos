
add_executable(gears gears.c)
set_module_type(gears win32cui)
add_importlibs(gears opengl32 gdi32 user32 msvcrt kernel32)
add_cd_file(TARGET gears DESTINATION reactos/bin FOR all)
