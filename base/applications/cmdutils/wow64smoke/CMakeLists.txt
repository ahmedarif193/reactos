add_executable(wow64smoke wow64smoke.c wow64smoke.rc)
set_module_type(wow64smoke win32cui)
add_importlibs(wow64smoke msvcrt kernel32)
add_cd_file(TARGET wow64smoke DESTINATION reactos/system32 FOR all)
