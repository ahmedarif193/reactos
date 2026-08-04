add_executable(acpitimectl acpitimectl.c acpitimectl.rc)
set_module_type(acpitimectl win32cui UNICODE)
add_importlibs(acpitimectl setupapi msvcrt kernel32)
add_cd_file(TARGET acpitimectl DESTINATION reactos/system32 FOR all)
