set(WINEDMO_FFMPEG_ROOT "$ENV{REACTOS_FFMPEG_ROOT}" CACHE PATH "Path to the target FFmpeg shared development package used by winedmo")

spec2def(winedmo.dll winedmo.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c)

if(EXISTS "${WINEDMO_FFMPEG_ROOT}/include/libavformat/avformat.h")
    function(winedmo_find_import_library output_name library_name)
        if(EXISTS "${WINEDMO_FFMPEG_ROOT}/lib/lib${library_name}.dll.a")
            set(${output_name} "${WINEDMO_FFMPEG_ROOT}/lib/lib${library_name}.dll.a" PARENT_SCOPE)
        elseif(EXISTS "${WINEDMO_FFMPEG_ROOT}/lib/${library_name}.lib")
            set(${output_name} "${WINEDMO_FFMPEG_ROOT}/lib/${library_name}.lib" PARENT_SCOPE)
        else()
            message(FATAL_ERROR "winedmo requires an import library for ${library_name}")
        endif()
    endfunction()

    winedmo_find_import_library(WINEDMO_AVFORMAT_IMPORT_LIBRARY avformat)
    winedmo_find_import_library(WINEDMO_AVCODEC_IMPORT_LIBRARY avcodec)
    winedmo_find_import_library(WINEDMO_AVUTIL_IMPORT_LIBRARY avutil)

    list(APPEND SOURCE
        reactos.c
        unix_demuxer.c
        unix_media_type.c)
else()
    message(STATUS "winedmo FFmpeg backend disabled; set WINEDMO_FFMPEG_ROOT to a target FFmpeg shared development package")
    list(APPEND SOURCE
        reactos_stubs.c)
endif()

add_library(winedmo MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/winedmo.def)
target_compile_definitions(winedmo PRIVATE __WINESRC__)
set_module_type(winedmo win32dll)
target_link_libraries(winedmo mfuuid wine)
add_importlibs(winedmo msvcrt kernel32 ntdll)
add_cd_file(TARGET winedmo DESTINATION reactos/system32 FOR all)

if(EXISTS "${WINEDMO_FFMPEG_ROOT}/include/libavformat/avformat.h")
    target_include_directories(winedmo PRIVATE "${WINEDMO_FFMPEG_ROOT}/include")
    target_compile_definitions(winedmo PRIVATE HAVE_FFMPEG HAVE_LIBAVCODEC_BSF_H)
    target_link_libraries(winedmo "${WINEDMO_AVFORMAT_IMPORT_LIBRARY}" "${WINEDMO_AVCODEC_IMPORT_LIBRARY}" "${WINEDMO_AVUTIL_IMPORT_LIBRARY}")
    add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/avformat-62.dll" DESTINATION reactos/system32 FOR all)
    add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/avcodec-62.dll" DESTINATION reactos/system32 FOR all)
    add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/avutil-60.dll" DESTINATION reactos/system32 FOR all)
    add_cd_file(FILE "${WINEDMO_FFMPEG_ROOT}/bin/swresample-6.dll" DESTINATION reactos/system32 FOR all)
endif()

set_wine_module(winedmo)
