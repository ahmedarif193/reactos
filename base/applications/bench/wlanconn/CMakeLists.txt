add_executable(wlanconn wlanconn.c wlanconn.rc)
set_module_type(wlanconn win32cui UNICODE)
add_importlibs(wlanconn wlanapi msvcrt kernel32)
add_cd_file(TARGET wlanconn DESTINATION reactos/system32 FOR all)
