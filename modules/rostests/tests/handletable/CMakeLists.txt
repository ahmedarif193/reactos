add_executable(handletable handletable.c)
set_module_type(handletable win32cui)
add_importlibs(handletable msvcrt kernel32)
add_rostests_file(TARGET handletable)
