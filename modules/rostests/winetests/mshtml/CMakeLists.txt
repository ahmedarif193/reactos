
add_definitions(-DUSE_WINE_TODOS)
add_idl_Headers(test_tlb_header test_tlb.idl)
add_typelib(test_tlb.idl)

list(APPEND SOURCE
    activex.c
    dom.c
    events.c
    htmldoc.c
    htmllocation.c
    misc.c
    protocol.c
    script.c
    style.c
    xmlhttprequest.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c
    testlist.c)

add_executable(mshtml_winetest
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    rsrc.rc)

set_source_files_properties(protocol.c PROPERTIES COMPILE_DEFINITIONS CONST_VTABLE)
list(APPEND mshtml_winetest_rc_deps
    ${CMAKE_CURRENT_BINARY_DIR}/test_tlb.tlb
    ${CMAKE_CURRENT_SOURCE_DIR}/asyncscriptload.js
    ${CMAKE_CURRENT_SOURCE_DIR}/blank.html
    ${CMAKE_CURRENT_SOURCE_DIR}/blank_ie10.html
    ${CMAKE_CURRENT_SOURCE_DIR}/doc_with_prop.html
    ${CMAKE_CURRENT_SOURCE_DIR}/doc_with_prop_ie9.html
    ${CMAKE_CURRENT_SOURCE_DIR}/documentmode.js
    ${CMAKE_CURRENT_SOURCE_DIR}/dom.js
    ${CMAKE_CURRENT_SOURCE_DIR}/es5.js
    ${CMAKE_CURRENT_SOURCE_DIR}/event_iframe.html
    ${CMAKE_CURRENT_SOURCE_DIR}/events.html
    ${CMAKE_CURRENT_SOURCE_DIR}/events.js
    ${CMAKE_CURRENT_SOURCE_DIR}/exectest.html
    ${CMAKE_CURRENT_SOURCE_DIR}/externscr.js
    ${CMAKE_CURRENT_SOURCE_DIR}/frame.js
    ${CMAKE_CURRENT_SOURCE_DIR}/iframe.html
    ${CMAKE_CURRENT_SOURCE_DIR}/img.png
    ${CMAKE_CURRENT_SOURCE_DIR}/jstest.html
    ${CMAKE_CURRENT_SOURCE_DIR}/navigation.js
    ${CMAKE_CURRENT_SOURCE_DIR}/reload.js
    ${CMAKE_CURRENT_SOURCE_DIR}/runscript.html
    ${CMAKE_CURRENT_SOURCE_DIR}/utf16.css
    ${CMAKE_CURRENT_SOURCE_DIR}/utf16.js
    ${CMAKE_CURRENT_SOURCE_DIR}/utf16_bom.css
    ${CMAKE_CURRENT_SOURCE_DIR}/utf16_bom.js
    ${CMAKE_CURRENT_SOURCE_DIR}/utf8.css
    ${CMAKE_CURRENT_SOURCE_DIR}/utf8.js
    ${CMAKE_CURRENT_SOURCE_DIR}/utf8_bom.css
    ${CMAKE_CURRENT_SOURCE_DIR}/utf8_bom.js
    ${CMAKE_CURRENT_SOURCE_DIR}/vbtest.html
    ${CMAKE_CURRENT_SOURCE_DIR}/winetest.js
    ${CMAKE_CURRENT_SOURCE_DIR}/xhr.js
    ${CMAKE_CURRENT_SOURCE_DIR}/xhr_iframe.html)

set_source_files_properties(rsrc.rc PROPERTIES OBJECT_DEPENDS "${mshtml_winetest_rc_deps}")
target_link_libraries(mshtml_winetest uuid strmiids)
set_module_type(mshtml_winetest win32cui)
add_importlibs(mshtml_winetest ole32 oleaut32 shlwapi wininet user32 urlmon gdi32 advapi32 msvcrt kernel32)
add_dependencies(mshtml_winetest test_tlb_header stdole2)
add_rostests_file(TARGET mshtml_winetest)
