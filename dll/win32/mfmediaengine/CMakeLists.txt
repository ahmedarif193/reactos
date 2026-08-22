spec2def(mfmediaengine.dll mfmediaengine.spec)

list(APPEND SOURCE
    main.c
    video_frame_sink.c)

add_library(mfmediaengine MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/mfmediaengine.def ${CMAKE_CURRENT_BINARY_DIR}/mfmediaengine_stubs.c)
add_idl_reg_scripts(mfmediaengine registry mediaengine_classes.idl)
set_module_type(mfmediaengine win32dll)
target_compile_definitions(mfmediaengine PRIVATE __WINESRC__)
target_link_libraries(mfmediaengine mfuuid uuid wine wine_dll_canunload)
add_importlibs(mfmediaengine oleaut32 ole32 mfplat mf msvcrt kernel32 ntdll)
add_cd_file(TARGET mfmediaengine DESTINATION reactos/system32 FOR all)
set_wine_module(mfmediaengine)
