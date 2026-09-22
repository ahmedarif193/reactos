
list(APPEND SOURCE
    ContextMenu.c
    EditStyle.c
    ExtendedClasses.c
    testlist.c)

add_executable(riched20_apitest ${SOURCE})
target_link_libraries(riched20_apitest wine ${PSEH_LIB})
set_module_type(riched20_apitest win32cui)
add_importlibs(riched20_apitest ole32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET riched20_apitest)
