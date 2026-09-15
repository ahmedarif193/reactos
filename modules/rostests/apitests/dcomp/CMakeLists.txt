add_executable(dcomp_apitest
    visual.c
    testlist.c)

set_module_type(dcomp_apitest win32cui)
add_importlibs(dcomp_apitest msvcrt kernel32)
add_rostests_file(TARGET dcomp_apitest)
