
spec2def(mscoree.dll mscoree.spec)

list(APPEND SOURCE
    assembly.c
    config.c
    cordebug.c
    corruntimehost.c
    metadata.c
    metahost.c
    mscoree_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/mscoree_stubs.c)

add_library(mscoree MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/mscoree.def)

set_source_files_properties(metahost.c PROPERTIES COMPILE_DEFINITIONS "INSTALL_DATADIR=\"/usr/share\"")
set_module_type(mscoree win32dll)
add_idl_reg_scripts(mscoree registry mscoree_classes.idl)
target_link_libraries(mscoree uuid wine wine_dll_canunload)
add_importlibs(mscoree dbghelp advapi32 shell32 kernel32_vista ole32 oleaut32 shlwapi msvcrt kernel32 ntdll)
add_cd_file(TARGET mscoree DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(mscoree) # CORE-5743: No CONST_VTABLE
