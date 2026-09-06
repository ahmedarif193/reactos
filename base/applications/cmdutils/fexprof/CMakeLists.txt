
add_executable(fexprof fexprof.c)
set_module_type(fexprof win32cui)
add_importlibs(fexprof msvcrt kernel32 ntdll)
add_cd_file(TARGET fexprof DESTINATION reactos/system32 FOR all)
