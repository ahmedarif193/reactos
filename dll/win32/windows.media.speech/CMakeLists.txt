spec2def(windows.media.speech.dll windows.media.speech.spec)
add_idl_headers(windows_media_speech_private_idl async_private.idl)

list(APPEND SOURCE
    async.c
    event_handlers.c
    listconstraint.c
    main.c
    recognizer.c
    synthesizer.c
    vector.c)

add_library(windows_media_speech MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.media.speech.def ${CMAKE_CURRENT_BINARY_DIR}/windows.media.speech_stubs.c)
set_target_properties(windows_media_speech PROPERTIES OUTPUT_NAME "windows.media.speech")
add_idl_reg_scripts(windows_media_speech winrt classes.idl)
add_dependencies(windows_media_speech windows_media_speech_private_idl)
set_module_type(windows_media_speech win32dll)
target_compile_definitions(windows_media_speech PRIVATE __WINESRC__)
target_link_libraries(windows_media_speech uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_media_speech combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_media_speech DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_media_speech)
