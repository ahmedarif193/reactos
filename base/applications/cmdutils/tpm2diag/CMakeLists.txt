add_executable(tpm2diag tpm2diag.c tpm2diag.rc)
set_module_type(tpm2diag win32cui UNICODE)
add_importlibs(tpm2diag setupapi msvcrt kernel32)
add_cd_file(TARGET tpm2diag DESTINATION reactos/system32 FOR all)
