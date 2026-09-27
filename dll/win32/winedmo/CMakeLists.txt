spec2def(winedmo.dll winedmo.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c)

if(FFMPEG_ROOT)
    list(APPEND SOURCE
        reactos.c
        unix_demuxer.c
        unix_media_type.c)
else()
    list(APPEND SOURCE
        reactos_stubs.c)
endif()

add_library(winedmo MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/winedmo.def)
target_compile_definitions(winedmo PRIVATE __WINESRC__)
set_module_type(winedmo win32dll)
target_link_libraries(winedmo mfuuid wine)
add_importlibs(winedmo msvcrt kernel32 ntdll)
add_cd_file(TARGET winedmo DESTINATION reactos/system32 FOR all)

if(FFMPEG_ROOT)
    target_include_directories(winedmo PRIVATE "${FFMPEG_ROOT}/include")
    target_compile_definitions(winedmo PRIVATE HAVE_FFMPEG HAVE_LIBAVCODEC_BSF_H)
    target_link_libraries(winedmo
        "${FFMPEG_ROOT}/lib/libavformat.dll.a"
        "${FFMPEG_ROOT}/lib/libavcodec.dll.a"
        "${FFMPEG_ROOT}/lib/libavutil.dll.a")
    add_dependencies(winedmo ffmpeg)
endif()

set_wine_module(winedmo)
