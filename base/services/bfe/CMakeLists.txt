
add_executable(bfe bfe.c)
set_module_type(bfe win32cui UNICODE)
add_importlibs(bfe advapi32 msvcrt kernel32)
add_cd_file(TARGET bfe DESTINATION reactos/system32 FOR all)
