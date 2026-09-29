add_library(jxr STATIC
    image/decode/JXRTranscode.c
    image/decode/decode.c
    image/decode/postprocess.c
    image/decode/segdec.c
    image/decode/strInvTransform.c
    image/decode/strPredQuantDec.c
    image/decode/strdec.c
    image/encode/encode.c
    image/encode/segenc.c
    image/encode/strFwdTransform.c
    image/encode/strPredQuantEnc.c
    image/encode/strenc.c
    image/sys/adapthuff.c
    image/sys/image.c
    image/sys/strPredQuant.c
    image/sys/strTransform.c
    image/sys/strcodec.c
    jxrgluelib/JXRGlue.c
    jxrgluelib/JXRGlueJxr.c
    jxrgluelib/JXRGluePFC.c
    jxrgluelib/JXRMeta.c)

target_include_directories(jxr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/jxrgluelib ${CMAKE_CURRENT_SOURCE_DIR}/image/sys)
target_compile_definitions(jxr PRIVATE __ANSI__ DISABLE_PERF_MEASUREMENT _DLL __USE_CRTIMP)
if(MSVC)
    target_compile_options(jxr PRIVATE /w)
else()
    target_compile_options(jxr PRIVATE -w)
endif()
