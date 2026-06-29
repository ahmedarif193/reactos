
add_executable(opengl32_apitest sw_extensions.c sw_pixelformat.c wgl_smoke.c testlist.c)
target_link_libraries(opengl32_apitest wine)
set_module_type(opengl32_apitest win32cui)
add_importlibs(opengl32_apitest opengl32 gdi32 user32 msvcrt kernel32)
add_rostests_file(TARGET opengl32_apitest)
add_cd_file(TARGET opengl32_apitest DESTINATION reactos/system32 FOR all)
