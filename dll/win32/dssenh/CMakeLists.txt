
spec2def(dssenh.dll dssenh.spec NO_PRIVATE_WARNINGS)

set_source_files_properties(rsrc.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dssenh.rgs)

add_library(dssenh MODULE
    main.c
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dssenh_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dssenh.def)

set_module_type(dssenh win32dll)
target_compile_definitions(dssenh PRIVATE __WINESRC__ __WINE_PE_BUILD)
target_link_libraries(dssenh wine wine_dll_register symcrypt)
add_importlibs(dssenh crypt32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dssenh DESTINATION reactos/system32 FOR all)
set_wine_module(dssenh)
