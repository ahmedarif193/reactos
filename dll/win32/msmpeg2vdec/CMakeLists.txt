
# The decoders share winedmo's FFmpeg shared development package.
set(WINEDMO_FFMPEG_ROOT "$ENV{REACTOS_FFMPEG_ROOT}" CACHE PATH "Path to the target FFmpeg shared development package used by winedmo")

if(NOT EXISTS "${WINEDMO_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(STATUS "msmpeg2vdec disabled; set WINEDMO_FFMPEG_ROOT to a target FFmpeg shared development package")
    return()
endif()

set(WINEGSTREAMER_DIR ${REACTOS_SOURCE_DIR}/dll/directx/wine/winegstreamer)

spec2def(msmpeg2vdec.dll msmpeg2vdec.spec)

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
target_include_directories(msmpeg2vdec PRIVATE "${WINEDMO_FFMPEG_ROOT}/include")
target_compile_definitions(msmpeg2vdec PRIVATE __WINESRC__)
set_module_type(msmpeg2vdec win32dll)
target_link_libraries(msmpeg2vdec strmbase mfuuid dmoguids strmiids wmcodecdspuuid uuid wine wine_dll_canunload wine_dll_register
    "${WINEDMO_FFMPEG_ROOT}/lib/libavcodec.dll.a"
    "${WINEDMO_FFMPEG_ROOT}/lib/libswresample.dll.a"
    "${WINEDMO_FFMPEG_ROOT}/lib/libavutil.dll.a")
add_importlibs(msmpeg2vdec mf mfplat ole32 oleaut32 user32 msvcrt kernel32 ntdll)
add_dependencies(msmpeg2vdec wineheaders dxsdk)
add_cd_file(TARGET msmpeg2vdec DESTINATION reactos/system32 FOR all)
set_wine_module(msmpeg2vdec)
