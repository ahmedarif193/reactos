list(APPEND SOURCE
    PSPropertyBag.c
    testlist.c)

add_executable(propsys_apitest ${SOURCE})
set_module_type(propsys_apitest win32cui)
target_link_libraries(propsys_apitest ${PSEH_LIB} uuid)
add_importlibs(propsys_apitest ole32 oleaut32 msvcrt kernel32)
add_rostests_file(TARGET propsys_apitest)
