
spec2def(ndfapi.dll ndfapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/ndfapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/ndfapi.def)

add_library(ndfapi MODULE ${SOURCE})
set_module_type(ndfapi win32dll)
target_link_libraries(ndfapi wine)
add_importlibs(ndfapi msvcrt kernel32 ntdll)
add_cd_file(TARGET ndfapi DESTINATION reactos/system32 FOR all)
