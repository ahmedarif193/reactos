
spec2def(hhctrl.ocx hhctrl.ocx.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    chm.c
    content.c
    help.c
    hhctrl.c
    index.c
    search.c
    stream.c
    webbrowser.c)

add_library(hhctrl MODULE
    ${SOURCE}
    hhctrl_ros.rc
    ${CMAKE_CURRENT_BINARY_DIR}/hhctrl.def)

add_typelib(hhctrl_tlb.idl)

list(APPEND hhctrl_rc_deps
    ${CMAKE_CURRENT_SOURCE_DIR}/hhctrl_tlb.rgs
    ${CMAKE_CURRENT_BINARY_DIR}/hhctrl_tlb.tlb)

set_source_files_properties(hhctrl_ros.rc PROPERTIES OBJECT_DEPENDS "${hhctrl_rc_deps}")
set_module_type(hhctrl win32ocx)
target_link_libraries(hhctrl uuid wine wine_dll_register)
add_importlibs(hhctrl advapi32 comctl32 shlwapi ole32 oleaut32 user32 gdi32 msvcrt kernel32 ntdll)
add_dependencies(hhctrl stdole2 wineheaders)
add_cd_file(TARGET hhctrl DESTINATION reactos/system32 FOR all)
target_compile_definitions(hhctrl PRIVATE __WINESRC__)
set_wine_module(hhctrl)
