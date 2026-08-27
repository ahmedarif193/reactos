add_executable(wddmuitest wddm_ui_test.c)
set_module_type(wddmuitest win32cui)
add_importlibs(wddmuitest opengl32 gdi32 user32 msvcrt kernel32)
add_cd_file(TARGET wddmuitest DESTINATION reactos/system32 FOR preinstall)
