spec2def(windows.media.dll windows.media.spec)

list(APPEND SOURCE
    captions.c
    main.c
    transcoder.c)

add_library(windows_media MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.media.def ${CMAKE_CURRENT_BINARY_DIR}/windows.media_stubs.c)
set_target_properties(windows_media PROPERTIES OUTPUT_NAME "windows.media")
add_idl_reg_scripts(windows_media winrt classes.idl)
set_module_type(windows_media win32dll)
target_compile_definitions(windows_media PRIVATE __WINESRC__)
target_link_libraries(windows_media uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_media combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_media DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_media)
