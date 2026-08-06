
add_definitions(
    -D__ROS_LONG64__
    -DCOM_NO_WINDOWS_H
    -Dstrcasecmp=_stricmp
    -Dstrncasecmp=_strnicmp
)

add_idl_headers(mshtml_iface_headers nsiface.idl mshtml_private_iface.idl)
add_typelib(mshtml_private_iface.idl)
spec2def(mshtml.dll mshtml.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    conpoint.c
    dispex.c
    editor.c
    htmlanchor.c
    htmlarea.c
    htmlattr.c
    htmlbody.c
    htmlcurstyle.c
    htmldoc.c
    htmlelem.c
    htmlelemcol.c
    htmlevent.c
    htmlform.c
    htmlframe.c
    htmlgeneric.c
    htmlhead.c
    htmlimg.c
    htmlinput.c
    htmllink.c
    htmllocation.c
    htmlnode.c
    htmlobject.c
    htmlscript.c
    htmlselect.c
    htmlstorage.c
    htmlstyle.c
    htmlstyleelem.c
    htmlstylesheet.c
    htmltable.c
    htmltextarea.c
    htmltextnode.c
    htmlwindow.c
    ifacewrap.c
    loadopts.c
    main.c
    mutation.c
    navigate.c
    npplugin.c
    nsembed.c
    nsevents.c
    nsio.c
    nsservice.c
    olecmd.c
    oleobj.c
    omnavigator.c
    persist.c
    pluginhost.c
    protocol.c
    range.c
    script.c
    secmgr.c
    selection.c
    service.c
    svg.c
    task.c
    view.c
    xmlhttprequest.c)

set_source_files_properties(nsembed.c PROPERTIES COMPILE_DEFINITIONS "INSTALL_DATADIR=\"\"")

if(MSVC)
    add_asm_files(mshtml_asm msvc.S)
endif()

list(APPEND PCH_SKIP_SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/mshtml_stubs.c)

add_library(mshtml MODULE
    ${SOURCE}
    ${mshtml_asm}
    ${PCH_SKIP_SOURCE}
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/mshtml.def)

list(APPEND mshtml_rc_deps
    ${CMAKE_CURRENT_BINARY_DIR}/mshtml_private_iface.tlb
    ${CMAKE_CURRENT_SOURCE_DIR}/blank.htm
    ${CMAKE_CURRENT_SOURCE_DIR}/mshtml.inf)

set_source_files_properties(rsrc.rc PROPERTIES OBJECT_DEPENDS "${mshtml_rc_deps}")
set_module_type(mshtml win32dll)
add_idl_reg_scripts(mshtml registry mshtml_classes.idl)
target_link_libraries(mshtml uuid wine wine_dll_canunload strmiids oldnames)
add_delay_importlibs(mshtml wininet inetcomm)
add_importlibs(mshtml urlmon shlwapi shell32 ole32 oleaut32 user32 gdi32 advapi32 msvcrt kernel32 ntdll)
add_dependencies(mshtml mshtml_iface_headers stdole2)
add_cd_file(TARGET mshtml DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(mshtml) # CORE-5743: No CONST_VTABLE
