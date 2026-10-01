
add_executable(pnputil pnputil.c pnputil.rc)
set_module_type(pnputil win32cui UNICODE)
target_link_libraries(pnputil conutils)
add_importlibs(pnputil setupapi newdev ole32 advapi32 user32 msvcrt kernel32)
add_cd_file(TARGET pnputil DESTINATION reactos/system32 FOR all)
