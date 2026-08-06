
add_definitions(-D__WINESRC__ -DWINE_REGISTER_DLL)
spec2def(amstream.dll amstream.spec)

list(APPEND SOURCE
    audiodata.c
    audiostream.c
    ddrawstream.c
    filter.c
    main.c
    multimedia.c)

add_library(amstream MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/amstream.def)

add_idl_reg_scripts(amstream registry amstream_classes.idl)
set_module_type(amstream win32dll)
target_link_libraries(amstream strmbase strmiids uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(amstream advapi32 ddraw ole32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET amstream DESTINATION reactos/system32 FOR all)
