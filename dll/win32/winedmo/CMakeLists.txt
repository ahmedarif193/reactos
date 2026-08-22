set(WINEDMO_FFMPEG_ROOT "$ENV{REACTOS_FFMPEG_ROOT}" CACHE PATH "Path to the target FFmpeg shared development package used by winedmo")

if(NOT EXISTS "${WINEDMO_FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(STATUS "winedmo disabled; set WINEDMO_FFMPEG_ROOT to a target FFmpeg shared development package")
    return()
endif()

spec2def(winedmo.dll winedmo.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    reactos.c
    unix_demuxer.c
    unix_media_type.c)

add_library(winedmo MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/winedmo.def)
target_include_directories(winedmo PRIVATE "${WINEDMO_FFMPEG_ROOT}/include")
target_compile_definitions(winedmo PRIVATE HAVE_FFMPEG HAVE_LIBAVCODEC_BSF_H __WINESRC__)
set_module_type(winedmo win32dll)
target_link_libraries(winedmo mfuuid wine "${WINEDMO_FFMPEG_ROOT}/lib/libavformat.dll.a" "${WINEDMO_FFMPEG_ROOT}/lib/libavcodec.dll.a" "${WINEDMO_FFMPEG_ROOT}/lib/libavutil.dll.a")
add_importlibs(winedmo msvcrt kernel32 ntdll)
add_cd_file(TARGET winedmo DESTINATION reactos/system32 FOR all)
add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/avformat-62.dll" DESTINATION reactos/system32 FOR all)
add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/avcodec-62.dll" DESTINATION reactos/system32 FOR all)
add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/avutil-60.dll" DESTINATION reactos/system32 FOR all)
add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/swresample-6.dll" DESTINATION reactos/system32 FOR all)
set_wine_module(winedmo)
