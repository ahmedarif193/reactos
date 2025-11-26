add_executable(usb3info usb3info.c)
set_module_type(usb3info win32cui)
add_importlibs(usb3info kernel32 user32)
add_cd_file(TARGET usb3info DESTINATION reactos/system32 FOR all)

