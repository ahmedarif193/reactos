
spec2def(dcomp.dll dcomp.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/dcomp_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dcomp.def)

add_library(dcomp MODULE ${SOURCE})
set_module_type(dcomp win32dll)
target_link_libraries(dcomp wine)
add_importlibs(dcomp msvcrt kernel32 ntdll)
add_cd_file(TARGET dcomp DESTINATION reactos/system32 FOR all)
