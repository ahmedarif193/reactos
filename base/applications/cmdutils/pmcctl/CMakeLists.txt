add_executable(pmcctl pmcctl.c pmcctl.rc)
set_module_type(pmcctl win32cui UNICODE)
add_importlibs(pmcctl setupapi msvcrt kernel32)
add_cd_file(TARGET pmcctl DESTINATION reactos/system32 FOR all)
