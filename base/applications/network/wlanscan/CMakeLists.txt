
add_executable(wlanscan wlanscan.c wlanscan.rc)
set_module_type(wlanscan win32cui UNICODE)
add_importlibs(wlanscan wlanapi msvcrt kernel32)
add_cd_file(TARGET wlanscan DESTINATION reactos/system32 FOR all)
