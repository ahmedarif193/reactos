
add_executable(holdmutex holdmutex.c)
set_module_type(holdmutex win32cui)
add_importlibs(holdmutex msvcrt kernel32)
add_cd_file(TARGET holdmutex DESTINATION reactos/system32 FOR all)
