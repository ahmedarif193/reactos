add_executable(ntfslx_auto ntfslx_auto.c)
set_module_type(ntfslx_auto win32cui UNICODE)
add_importlibs(ntfslx_auto ntdll msvcrt kernel32)
add_rostests_file(TARGET ntfslx_auto)
