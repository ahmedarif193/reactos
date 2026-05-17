
spec2def(joy.cpl joy.spec)
add_rc_deps(joy.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/joy.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/joy.manifest)

add_library(joy MODULE
    dinput.c
    main.c
    xinput.c
    joy.rc
    ${CMAKE_CURRENT_BINARY_DIR}/joy.def)

set_module_type(joy cpl UNICODE)
target_link_libraries(joy dinput_data_formats dxguid uuid wine)
add_importlibs(joy dinput dinput8 xinput1_3 ole32 comctl32 user32 gdi32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET joy DESTINATION reactos/system32 FOR all)
set_wine_module(joy)
