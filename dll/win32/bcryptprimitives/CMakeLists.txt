spec2def(bcryptprimitives.dll bcryptprimitives.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    bcryptprimitives.c
    ${CMAKE_CURRENT_BINARY_DIR}/bcryptprimitives_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/bcryptprimitives.def)

add_library(bcryptprimitives MODULE ${SOURCE})
set_module_type(bcryptprimitives win32dll)
add_importlibs(bcryptprimitives advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET bcryptprimitives DESTINATION reactos/system32 FOR all)
