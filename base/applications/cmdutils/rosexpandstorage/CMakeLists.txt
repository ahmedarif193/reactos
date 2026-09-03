add_executable(rosexpandstorage rosexpandstorage.c expandcore.c)
set_module_type(rosexpandstorage win32cui)
add_importlibs(rosexpandstorage msvcrt kernel32 advapi32 user32)
add_cd_file(TARGET rosexpandstorage DESTINATION reactos/system32 FOR all)
