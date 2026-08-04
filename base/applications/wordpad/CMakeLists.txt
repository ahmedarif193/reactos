
list(APPEND SOURCE
    olecallback.c
    print.c
    registry.c
    wordpad.c)

add_rc_deps(wordpad.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/formatbar.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/toolbar.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/wordpad.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/wordpad.manifest
    ${CMAKE_CURRENT_SOURCE_DIR}/zoom.cur)
add_rc_deps(rsrc.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/document.ico)
add_executable(wordpad ${SOURCE} wordpad.rc rsrc.rc)
set_module_type(wordpad win32gui)
target_link_libraries(wordpad wine uuid)
set_wine_module_FIXME(wordpad)
add_importlibs(wordpad comdlg32 ole32 shell32 user32 gdi32 advapi32 comctl32 msvcrt kernel32 ntdll)
add_cd_file(TARGET wordpad DESTINATION reactos/system32 FOR all)
add_cd_file(TARGET wordpad DESTINATION "Program Files/Windows NT/Accessories" FOR all)

if(MSVC)
    # Disable warning C4477 (printf format warnings)
    remove_target_compile_option(wordpad "/we4477")
endif()
