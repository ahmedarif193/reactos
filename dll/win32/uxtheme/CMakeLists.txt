
add_definitions(-D__WINESRC__ -D_UXTHEME_)

spec2def(uxtheme.dll uxtheme.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    buffer.c
    dialog.c
    draw.c
    errinfo.c
    main.c
    metric.c
    msstyles.c
    ncscrollbar.c
    nonclient.c
    property.c
    reactos.c
    resource.h
    scrollbar.c
    stylemap.c
    system.c
    themehooks.c
    uxini.c
    uxthemep.h
    window.c
    ${CMAKE_CURRENT_BINARY_DIR}/uxtheme_stubs.c)

if(DLL_EXPORT_VERSION GREATER_EQUAL 0x600)
    add_definitions(-DENABLE_PNG_SUPPORT)

    list(APPEND SOURCE
        pngsup.cpp)
endif()

file(GLOB uxtheme_rc_deps ${CMAKE_CURRENT_SOURCE_DIR}/lang/*.rc)
add_rc_deps(uxtheme.rc ${uxtheme_rc_deps})

add_library(uxtheme MODULE
    ${SOURCE}
    rsrc.rc
    uxtheme.rc
    ${CMAKE_CURRENT_BINARY_DIR}/uxtheme.def)

set_module_type(uxtheme win32dll)
target_link_libraries(uxtheme wine ${PSEH_LIB})
add_delay_importlibs(uxtheme msimg32)
if(DLL_EXPORT_VERSION GREATER_EQUAL 0x600)
    add_importlibs(uxtheme user32_vista user32 advapi32 gdiplus gdi32 shlwapi msvcrt kernel32 ntdll)
else()
    add_importlibs(uxtheme user32_vista user32 advapi32 gdi32 msvcrt kernel32_vista kernel32 ntdll)
endif()
add_cd_file(TARGET uxtheme DESTINATION reactos/system32 FOR all)
