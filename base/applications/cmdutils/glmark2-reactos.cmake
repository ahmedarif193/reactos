# PROJECT:     ReactOS Raspberry Pi 5 graphics validation
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the unmodified upstream glmark2 Win32/WGL flavor
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

set(GLMARK2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glmark2")

set(GLMARK2_ZLIB_SOURCES
    ${GLMARK2_SOURCE_DIR}/src/zlib/adler32.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/compress.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/crc32.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/deflate.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/gzclose.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/gzlib.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/gzread.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/gzwrite.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/infback.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/inffast.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/inflate.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/inftrees.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/trees.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/uncompr.c
    ${GLMARK2_SOURCE_DIR}/src/zlib/zutil.c)

set(GLMARK2_PNG_SOURCES
    ${GLMARK2_SOURCE_DIR}/src/libpng/png.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngerror.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pnggccrd.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngget.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngmem.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngpread.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngread.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngrio.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngrtran.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngrutil.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngset.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngtrans.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngvcrd.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngwio.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngwrite.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngwtran.c
    ${GLMARK2_SOURCE_DIR}/src/libpng/pngwutil.c)

add_library(glmark2-zlib STATIC ${GLMARK2_ZLIB_SOURCES})
target_include_directories(glmark2-zlib BEFORE PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/ucrt
    ${GLMARK2_SOURCE_DIR}/src/zlib)
target_compile_definitions(glmark2-zlib PRIVATE
    WIN32
    _CRT_DECLARE_NONSTDC_NAMES=1
    _DLL
    _WIN32
    _WINDOWS
    __USE_CRTIMP)

add_library(glmark2-png STATIC ${GLMARK2_PNG_SOURCES})
target_include_directories(glmark2-png BEFORE PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/ucrt
    ${GLMARK2_SOURCE_DIR}/src/libpng
    ${GLMARK2_SOURCE_DIR}/src/zlib)
target_compile_definitions(glmark2-png PRIVATE
    PNG_STATIC
    WIN32
    _CRT_DECLARE_NONSTDC_NAMES=1
    _DLL
    _WIN32
    _WINDOWS
    __USE_CRTIMP)
target_link_libraries(glmark2-png glmark2-zlib)

set(GLMARK2_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/glmark2-reactos-startup.c
    ${GLMARK2_SOURCE_DIR}/src/benchmark-collection.cpp
    ${GLMARK2_SOURCE_DIR}/src/benchmark.cpp
    ${GLMARK2_SOURCE_DIR}/src/canvas-generic.cpp
    ${GLMARK2_SOURCE_DIR}/src/gl-headers.cpp
    ${GLMARK2_SOURCE_DIR}/src/gl-state-wgl.cpp
    ${GLMARK2_SOURCE_DIR}/src/gl-visual-config.cpp
    ${GLMARK2_SOURCE_DIR}/src/image-reader.cpp
    ${GLMARK2_SOURCE_DIR}/src/libmatrix/log.cc
    ${GLMARK2_SOURCE_DIR}/src/libmatrix/mat.cc
    ${GLMARK2_SOURCE_DIR}/src/libmatrix/program.cc
    ${GLMARK2_SOURCE_DIR}/src/libmatrix/shader-source.cc
    ${GLMARK2_SOURCE_DIR}/src/libmatrix/util.cc
    ${GLMARK2_SOURCE_DIR}/src/main-loop.cpp
    ${GLMARK2_SOURCE_DIR}/src/main.cpp
    ${GLMARK2_SOURCE_DIR}/src/mesh.cpp
    ${GLMARK2_SOURCE_DIR}/src/model.cpp
    ${GLMARK2_SOURCE_DIR}/src/native-state-win32.cpp
    ${GLMARK2_SOURCE_DIR}/src/options.cpp
    ${GLMARK2_SOURCE_DIR}/src/results-file.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-buffer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-build.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-bump.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-clear.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-conditionals.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-default-options.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-desktop.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-effect-2d.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-function.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-grid.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/a.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/d.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/e.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/i.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/lamp.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/logo.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/m.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/n.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/o.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/s.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/splines.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/t.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas/table.cc
    ${GLMARK2_SOURCE_DIR}/src/scene-jellyfish.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-loop.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-pulsar.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-refract.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-shading.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-shadow.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/base-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/blur-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/copy-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/luminance-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/normal-from-height-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/overlay-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/renderer-chain.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/simplex-noise-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/terrain-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain/texture-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene-texture.cpp
    ${GLMARK2_SOURCE_DIR}/src/scene.cpp
    ${GLMARK2_SOURCE_DIR}/src/shared-library.cpp
    ${GLMARK2_SOURCE_DIR}/src/text-renderer.cpp
    ${GLMARK2_SOURCE_DIR}/src/texture.cpp
    ${GLMARK2_SOURCE_DIR}/src/glad/src/gl.c
    ${GLMARK2_SOURCE_DIR}/src/glad/src/wgl.c)

add_executable(glmark2-win32 ${GLMARK2_SOURCES})
set_property(TARGET glmark2-win32 PROPERTY CXX_STANDARD 17)
set_property(TARGET glmark2-win32 PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(glmark2-win32 BEFORE PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/ucrt
    ${GLMARK2_SOURCE_DIR}/src
    ${GLMARK2_SOURCE_DIR}/src/libmatrix
    ${GLMARK2_SOURCE_DIR}/src/scene-ideas
    ${GLMARK2_SOURCE_DIR}/src/scene-terrain
    ${GLMARK2_SOURCE_DIR}/src/glad/include
    ${GLMARK2_SOURCE_DIR}/src/libpng
    ${GLMARK2_SOURCE_DIR}/src/zlib
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/libjpeg
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)
target_compile_definitions(glmark2-win32 PRIVATE
    GLMARK_VERSION="2023.01"
    GLMARK_DATA_PATH="C:/ReactOS/system32/glmark2"
    GLMARK2_EXECUTABLE="glmark2-win32"
    GLMARK2_USE_GL=1
    GLMARK2_USE_WIN32=1
    GLMARK2_USE_WGL=1
    _USE_MATH_DEFINES
    PNG_STATIC
    WIN32
    _WIN32)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(glmark2-win32 PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/glmark2-reactos-compat.h
        -fexceptions
        -frtti
        -O2)
endif()
# The target-local startup bridge exposes LLVM's .ctors to the normal UCRT
# startup path, keeping every FILE operation and the process startup in UCRT.
target_link_libraries(glmark2-win32 cpprt getopt glmark2-png glmark2-zlib)
set_module_type(glmark2-win32 win32cui)
add_importlibs(glmark2-win32 libjpeg opengl32 gdi32 user32 ucrtbase kernel32 ntdll)
add_cd_file(TARGET glmark2-win32 DESTINATION reactos/system32 FOR all)

add_executable(glmark2_runner glmark2-runner.c)
set_module_type(glmark2_runner win32cui)
add_importlibs(glmark2_runner user32 msvcrt kernel32)
add_cd_file(TARGET glmark2_runner DESTINATION reactos/system32 FOR all)

file(GLOB_RECURSE GLMARK2_DATA_FILES RELATIVE "${GLMARK2_SOURCE_DIR}/data"
    "${GLMARK2_SOURCE_DIR}/data/models/*"
    "${GLMARK2_SOURCE_DIR}/data/shaders/*"
    "${GLMARK2_SOURCE_DIR}/data/textures/*")
foreach(GLMARK2_DATA_FILE IN LISTS GLMARK2_DATA_FILES)
    get_filename_component(GLMARK2_DATA_DIRECTORY "${GLMARK2_DATA_FILE}" DIRECTORY)
    add_cd_file(
        FILE "${GLMARK2_SOURCE_DIR}/data/${GLMARK2_DATA_FILE}"
        DESTINATION "reactos/system32/glmark2/${GLMARK2_DATA_DIRECTORY}"
        NO_CAB
        FOR all)
endforeach()
