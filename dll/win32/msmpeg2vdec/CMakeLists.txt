
spec2def(msmpeg2vdec.dll msmpeg2vdec.spec)

# Without FFmpeg the module still ships, so setup can register it, but it
# neither registers nor creates the decoders.
if(NOT FFMPEG_ROOT)
    add_library(msmpeg2vdec MODULE
        stubs.c
        ${CMAKE_CURRENT_BINARY_DIR}/msmpeg2vdec_stubs.c
        ${CMAKE_CURRENT_BINARY_DIR}/msmpeg2vdec.def)
    set_module_type(msmpeg2vdec win32dll)
    target_link_libraries(msmpeg2vdec wine_dll_canunload)
    add_importlibs(msmpeg2vdec msvcrt kernel32 ntdll)
    add_cd_file(TARGET msmpeg2vdec DESTINATION reactos/system32 FOR all)
    return()
endif()

set(WINEGSTREAMER_DIR ${REACTOS_SOURCE_DIR}/dll/directx/wine/winegstreamer)

list(APPEND SOURCE
    main.c
    ffmpeg.c
    ${WINEGSTREAMER_DIR}/aac_decoder.c
    ${WINEGSTREAMER_DIR}/video_decoder.c
    ${WINEGSTREAMER_DIR}/wg_sample.c)

add_library(msmpeg2vdec MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msmpeg2vdec_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msmpeg2vdec.def)

add_idl_reg_scripts(msmpeg2vdec registry msmpeg2vdec_classes.idl)
target_include_directories(msmpeg2vdec BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${WINEGSTREAMER_DIR})
target_include_directories(msmpeg2vdec PRIVATE "${FFMPEG_ROOT}/include")
target_compile_definitions(msmpeg2vdec PRIVATE __WINESRC__)
set_module_type(msmpeg2vdec win32dll)
target_link_libraries(msmpeg2vdec strmbase mfuuid dmoguids strmiids wmcodecdspuuid uuid wine wine_dll_canunload wine_dll_register
    "${FFMPEG_ROOT}/lib/libavcodec.dll.a"
    "${FFMPEG_ROOT}/lib/libswresample.dll.a"
    "${FFMPEG_ROOT}/lib/libavutil.dll.a")
add_importlibs(msmpeg2vdec mf mfplat ole32 oleaut32 user32 msvcrt kernel32 ntdll)
add_dependencies(msmpeg2vdec wineheaders dxsdk ffmpeg)
add_cd_file(TARGET msmpeg2vdec DESTINATION reactos/system32 FOR all)
set_wine_module(msmpeg2vdec)
