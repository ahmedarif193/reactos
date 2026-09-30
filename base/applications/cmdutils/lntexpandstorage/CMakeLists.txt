add_executable(lntexpandstorage lntexpandstorage.c expandcore.c)
set_module_type(lntexpandstorage win32cui)
add_importlibs(lntexpandstorage msvcrt kernel32 advapi32 user32)
add_cd_file(TARGET lntexpandstorage DESTINATION reactos/system32 FOR all)
