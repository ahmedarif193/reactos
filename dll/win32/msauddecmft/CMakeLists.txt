
# The AAC decoder lives in msmpeg2vdec; applications load it by this name.
set(WINEDMO_FFMPEG_ROOT "$ENV{REACTOS_FFMPEG_ROOT}" CACHE PATH "Path to the target FFmpeg shared development package used by winedmo")

if(NOT EXISTS "${WINEDMO_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    return()
endif()

spec2def(msauddecmft.dll msauddecmft.spec)

add_library(msauddecmft MODULE ${CMAKE_CURRENT_BINARY_DIR}/msauddecmft.def)
set_target_properties(msauddecmft PROPERTIES LINKER_LANGUAGE C)
set_module_type(msauddecmft win32dll ENTRYPOINT 0)
add_dependencies(msauddecmft msmpeg2vdec)
add_cd_file(TARGET msauddecmft DESTINATION reactos/system32 FOR all)
