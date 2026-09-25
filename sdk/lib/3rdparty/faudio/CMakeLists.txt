
add_definitions(
    -DFAUDIO_WIN32_PLATFORM
    -DHAVE_WMADEC
    -Dalloca=_alloca)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

list(APPEND SOURCE
    src/F3DAudio.c
    src/FACT.c
    src/FACT3D.c
    src/FACT_internal.c
    src/FAPOBase.c
    src/FAPOFX.c
    src/FAPOFX_echo.c
    src/FAPOFX_eq.c
    src/FAPOFX_masteringlimiter.c
    src/FAPOFX_reverb.c
    src/FAudio.c
    src/FAudioFX_reverb.c
    src/FAudioFX_volumemeter.c
    src/FAudio_internal.c
    src/FAudio_internal_simd.c
    src/FAudio_operationset.c
    src/FAudio_platform_win32.c)

add_library(faudio ${SOURCE})
target_compile_definitions(faudio PRIVATE $<$<CONFIG:Debug>:NDEBUG>)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(faudio PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable)
endif()

add_dependencies(faudio psdk)
