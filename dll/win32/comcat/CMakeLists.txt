
spec2def(comcat.dll comcat.spec)

list(APPEND SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/comcat.def)

add_library(comcat MODULE ${SOURCE} version.rc)
set_module_type(comcat win32dll)
target_link_libraries(comcat wine wine_dll_canunload wine_dll_register)
add_importlibs(comcat ole32 msvcrt kernel32 ntdll)
add_cd_file(TARGET comcat DESTINATION reactos/system32 FOR all)
set_wine_module(comcat)
