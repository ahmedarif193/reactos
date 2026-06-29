list(APPEND SOURCE
    DisplayStackClassifier.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(display_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

set_module_type(display_apitest win32cui)
target_link_libraries(display_apitest wine)
add_importlibs(display_apitest gdi32 user32 msvcrt kernel32 ntdll)
add_pch(display_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET display_apitest)
add_cd_file(TARGET display_apitest DESTINATION reactos/system32 FOR all)
