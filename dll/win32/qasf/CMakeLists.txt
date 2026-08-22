spec2def(qasf.dll qasf.spec)

list(APPEND SOURCE
    asfreader.c
    dmowrapper.c
    qasf_main.c)

add_library(qasf MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/qasf.def ${CMAKE_CURRENT_BINARY_DIR}/qasf_stubs.c)
add_idl_reg_scripts(qasf registry qasf_classes.idl)
set_module_type(qasf win32dll)
target_compile_definitions(qasf PRIVATE __WINESRC__)
target_link_libraries(qasf strmbase dmoguids strmiids uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(qasf ole32 oleaut32 user32 wmvcore msvcrt kernel32 ntdll)
add_cd_file(TARGET qasf DESTINATION reactos/system32 FOR all)
set_wine_module(qasf)
