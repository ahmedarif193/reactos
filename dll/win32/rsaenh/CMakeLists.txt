
spec2def(rsaenh.dll rsaenh.spec ADD_IMPORTLIB NO_PRIVATE_WARNINGS)

set_source_files_properties(rsrc.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rsaenh.rgs)

list(APPEND SOURCE
    handle.c
    implglue.c
    rsaenh.c)

add_library(rsaenh MODULE
    ${SOURCE}
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/rsaenh.def)

set_module_type(rsaenh win32dll)
target_compile_definitions(rsaenh PRIVATE __WINESRC__ __WINE_PE_BUILD)
target_link_libraries(rsaenh wine wine_dll_register symcrypt)
add_importlibs(rsaenh msvcrt crypt32 advapi32 kernel32 ntdll)
add_cd_file(TARGET rsaenh DESTINATION reactos/system32 FOR all)
set_wine_module(rsaenh)
