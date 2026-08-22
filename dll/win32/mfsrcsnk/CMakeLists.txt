spec2def(mfsrcsnk.dll mfsrcsnk.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    factory.c
    media_sink.c
    media_source.c
    wave.c)

set_source_files_properties(mfsrcsnk.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mfsrcsnk.rgs)
add_library(mfsrcsnk MODULE ${SOURCE} mfsrcsnk.rc ${CMAKE_CURRENT_BINARY_DIR}/mfsrcsnk.def ${CMAKE_CURRENT_BINARY_DIR}/mfsrcsnk_stubs.c)
add_idl_reg_scripts(mfsrcsnk registry mfsrcsnk.idl)
set_module_type(mfsrcsnk win32dll)
target_compile_definitions(mfsrcsnk PRIVATE __WINESRC__)
target_link_libraries(mfsrcsnk mfuuid uuid wine wine_dll_register)
add_importlibs(mfsrcsnk advapi32 ole32 mfplat winedmo msvcrt kernel32 ntdll)
add_cd_file(TARGET mfsrcsnk DESTINATION reactos/system32 FOR all)
set_wine_module(mfsrcsnk)
