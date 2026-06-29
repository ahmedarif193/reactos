
list(APPEND SOURCE
    watchdog_test.c)

add_executable(watchdog_test ${SOURCE})
set_module_type(watchdog_test win32cui)
add_dependencies(watchdog_test psdk)
add_importlibs(watchdog_test gdi32 advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET watchdog_test)
