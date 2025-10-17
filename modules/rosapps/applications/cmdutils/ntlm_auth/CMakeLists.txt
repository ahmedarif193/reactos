add_executable(ntlm_auth ntlm_auth.c ntlm_auth.rc)
set_module_type(ntlm_auth win32cui)
add_importlibs(ntlm_auth msvcrt kernel32)
add_cd_file(TARGET ntlm_auth DESTINATION reactos/system32 FOR all)
