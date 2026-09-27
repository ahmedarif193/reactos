# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
#
# LGPL FFmpeg libraries for winedmo and msmpeg2vdec, built from the ffmpeg
# source feed. Upstream sources retain their original licenses.
#
# Sets FFMPEG_ROOT to the installed package (bin, include, lib) when FFmpeg is
# built; the modules that use it fall back to their FFmpeg-less build otherwise.

set(FFMPEG_ROOT)
if(NOT ENABLE_FFMPEG)
    return()
endif()

set(FFMPEG_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/ffmpeg")
if(NOT EXISTS "${FFMPEG_SOURCE_DIR}/configure")
    message(STATUS "FFmpeg: the ffmpeg feed is not checked out; building the decoders without it. "
        "Run scripts/feeds update ffmpeg to build it.")
    return()
endif()

if(ARCH STREQUAL "arm64")
    set(_ffmpeg_cpu aarch64)
elseif(ARCH STREQUAL "amd64")
    set(_ffmpeg_cpu x86_64)
elseif(ARCH STREQUAL "i386")
    set(_ffmpeg_cpu i686)
else()
    message(FATAL_ERROR "FFmpeg does not support ARCH ${ARCH}. Use -DENABLE_FFMPEG=OFF.")
endif()

find_program(FFMPEG_CC NAMES ${_ffmpeg_cpu}-w64-mingw32-clang HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin" NO_CACHE)
find_program(FFMPEG_SH NAMES sh NO_CACHE)
find_program(FFMPEG_MAKE NAMES gmake make NO_CACHE)
if(NOT FFMPEG_CC OR NOT FFMPEG_SH OR NOT FFMPEG_MAKE)
    message(FATAL_ERROR "FFmpeg needs sh, make and the ${_ffmpeg_cpu}-w64-mingw32 llvm-mingw Clang. "
        "Use -DENABLE_FFMPEG=OFF to disable it.")
endif()
get_filename_component(_ffmpeg_toolchain_bin "${FFMPEG_CC}" DIRECTORY)
set(_ffmpeg_cross_prefix "${_ffmpeg_toolchain_bin}/${_ffmpeg_cpu}-w64-mingw32-")

set(_ffmpeg_asm_args)
if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    find_program(FFMPEG_NASM NAMES nasm NO_CACHE)
    if(FFMPEG_NASM)
        set(_ffmpeg_asm_args --x86asmexe=${FFMPEG_NASM})
    else()
        set(_ffmpeg_asm_args --disable-x86asm)
    endif()
endif()

set(FFMPEG_ROOT "${REACTOS_BINARY_DIR}/submodules/ffmpeg-install")
set(FFMPEG_DLLS avformat-62 avcodec-62 avutil-60 swresample-6)
set(FFMPEG_LICENSE "${FFMPEG_ROOT}/FFmpeg-COPYING.LGPLv2.1.txt")
set(_ffmpeg_byproducts "${FFMPEG_LICENSE}")
foreach(_ffmpeg_lib avformat avcodec avutil swresample)
    list(APPEND _ffmpeg_byproducts "${FFMPEG_ROOT}/lib/lib${_ffmpeg_lib}.dll.a")
endforeach()
foreach(_ffmpeg_dll IN LISTS FFMPEG_DLLS)
    list(APPEND _ffmpeg_byproducts "${FFMPEG_ROOT}/bin/${_ffmpeg_dll}.dll")
endforeach()
cmake_host_system_information(RESULT _ffmpeg_jobs QUERY NUMBER_OF_LOGICAL_CORES)

include(ExternalProject)
# The configuration is LGPL-2.1-or-later: no GPL, version3 or nonfree parts.
ExternalProject_Add(ffmpeg
    EXCLUDE_FROM_ALL TRUE
    PREFIX "${REACTOS_BINARY_DIR}/submodules/ffmpeg-prefix"
    SOURCE_DIR "${FFMPEG_SOURCE_DIR}"
    BINARY_DIR "${REACTOS_BINARY_DIR}/submodules/ffmpeg-build"
    INSTALL_DIR "${FFMPEG_ROOT}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND ${FFMPEG_SH} <SOURCE_DIR>/configure
        --prefix=<INSTALL_DIR>
        --enable-cross-compile
        --target-os=mingw32
        --arch=${_ffmpeg_cpu}
        --cross-prefix=${_ffmpeg_cross_prefix}
        --cc=${_ffmpeg_cross_prefix}clang
        --cxx=${_ffmpeg_cross_prefix}clang++
        --pkg-config=false
        --enable-shared
        --disable-static
        --disable-programs
        --disable-doc
        --disable-network
        --disable-autodetect
        --enable-w32threads
        ${_ffmpeg_asm_args}
        --disable-avdevice
        --disable-avfilter
        --disable-swscale
        --disable-everything
        --enable-swresample
        --enable-decoder=h264,aac,aac_latm
        --enable-parser=h264,aac,aac_latm,mpegaudio
        --enable-demuxer=mov,avi,wav,asf,mpegps,mp3,aac,h264
        --enable-bsf=h264_mp4toannexb,aac_adtstoasc,null
        --enable-protocol=file
    BUILD_COMMAND ${FFMPEG_MAKE} -j${_ffmpeg_jobs}
    INSTALL_COMMAND ${FFMPEG_MAKE} install
    COMMAND ${CMAKE_COMMAND} -E copy_if_different <SOURCE_DIR>/COPYING.LGPLv2.1 "${FFMPEG_LICENSE}"
    BUILD_BYPRODUCTS ${_ffmpeg_byproducts}
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
    LOG_OUTPUT_ON_FAILURE TRUE)
# A feed update to another release reconfigures and rebuilds it.
ExternalProject_Add_StepDependencies(ffmpeg configure "${FFMPEG_SOURCE_DIR}/RELEASE")

foreach(_ffmpeg_dll IN LISTS FFMPEG_DLLS)
    add_cd_file(FILE "${FFMPEG_ROOT}/bin/${_ffmpeg_dll}.dll" TARGET ffmpeg DESTINATION reactos/system32 FOR all)
    # Processes restricted to OS-signed images load these through msmpeg2vdec.
    set_property(GLOBAL APPEND PROPERTY CI_SYSTEM_FILES "${FFMPEG_ROOT}/bin/${_ffmpeg_dll}.dll")
endforeach()
add_cd_file(FILE "${FFMPEG_LICENSE}" TARGET ffmpeg DESTINATION reactos/3rdParty FOR all)
