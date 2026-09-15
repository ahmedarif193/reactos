# SPDX-License-Identifier: MIT
# Compiler settings for the llvm-mingw ReactOS profiles.
list(APPEND MESA_C_OPTIONS
    "-D_FILE_OFFSET_BITS=64"
    "-Wall"
    "-D__STDC_CONSTANT_MACROS"
    "-D__STDC_FORMAT_MACROS"
    "-D__STDC_LIMIT_MACROS"
    "-DPACKAGE_VERSION=\"${MESA_VERSION}\""
    "-DPACKAGE_BUGREPORT=\"https://gitlab.freedesktop.org/mesa/mesa/-/issues\""
    "-DHAVE_OPENGL=1"
    "-DHAVE_OPENGL_ES_1=0"
    "-DHAVE_OPENGL_ES_2=0"
    "-DHAVE_SWRAST"
    "-DMESA_SYSTEM_HAS_KMS_DRM=0"
    "-DVIDEO_CODEC_VC1DEC=0"
    "-DVIDEO_CODEC_H264DEC=0"
    "-DVIDEO_CODEC_H264ENC=0"
    "-DVIDEO_CODEC_H265DEC=0"
    "-DVIDEO_CODEC_H265ENC=0"
    "-DVIDEO_CODEC_AV1DEC=0"
    "-DVIDEO_CODEC_AV1ENC=0"
    "-DVIDEO_CODEC_VP9DEC=0"
    "-DVIDEO_CODEC_MPEG12DEC=0"
    "-DVIDEO_CODEC_JPEGDEC=0"
    "-DHAVE_WINDOWS_PLATFORM"
    "-DUSE_LIBGLVND=0"
    "-DHAVE_GFX_COMPUTE"
    "-DGLAPI_EXPORT_PROTO_ENTRY_POINTS=1"
    "-DALLOW_KCMP"
    "-DMESA_DEBUG=0"
    "-DHAVE___BUILTIN_BSWAP32"
    "-DHAVE___BUILTIN_BSWAP64"
    "-DHAVE___BUILTIN_CLZ"
    "-DHAVE___BUILTIN_CLZLL"
    "-DHAVE___BUILTIN_CTZ"
    "-DHAVE___BUILTIN_EXPECT"
    "-DHAVE___BUILTIN_FFS"
    "-DHAVE___BUILTIN_FFSLL"
    "-DHAVE___BUILTIN_POPCOUNT"
    "-DHAVE___BUILTIN_POPCOUNTLL"
    "-DHAVE___BUILTIN_UNREACHABLE"
    "-DHAVE___BUILTIN_TYPES_COMPATIBLE_P"
    "-DHAVE___BUILTIN_ADD_OVERFLOW"
    "-DHAVE_FUNC_ATTRIBUTE_CONST"
    "-DHAVE_FUNC_ATTRIBUTE_FLATTEN"
    "-DHAVE_FUNC_ATTRIBUTE_MALLOC"
    "-DHAVE_FUNC_ATTRIBUTE_PURE"
    "-DHAVE_FUNC_ATTRIBUTE_UNUSED"
    "-DHAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT"
    "-DHAVE_FUNC_ATTRIBUTE_WEAK"
    "-DHAVE_FUNC_ATTRIBUTE_FORMAT"
    "-DHAVE_FUNC_ATTRIBUTE_PACKED"
    "-DHAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL"
    "-DHAVE_FUNC_ATTRIBUTE_ALIAS"
    "-DHAVE_FUNC_ATTRIBUTE_NORETURN"
    "-DHAVE_FUNC_ATTRIBUTE_COLD"
    "-DHAVE_FUNC_ATTRIBUTE_VISIBILITY"
    "-D_WIN32_WINNT=0x0A00"
    "-DWINVER=0x0A00"
    "-D_GNU_SOURCE"
    "-DUSE_GCC_ATOMIC_BUILTINS"
    "-DHAS_SCHED_H"
    "-DHAVE_ENDIAN_H"
    "-DHAVE_CET_H"
    "-DHAVE_STRTOF"
    "-DHAVE_STRTOK_R"
    "-DHAVE_QSORT_S"
    "-DHAVE_STRUCT_TIMESPEC"
    "-DWIN32_LEAN_AND_MEAN"
    "-DGALLIVM_USE_ORCJIT=0"
    "-DTHREAD_SANITIZER=0"
    "-DHAVE_RENDERDOC_INTEGRATION=0"
    "-Werror=implicit-function-declaration"
    "-Werror=missing-prototypes"
    "-Werror=return-type"
    "-Werror=empty-body"
    "-Werror=incompatible-pointer-types"
    "-Werror=int-conversion"
    "-Wimplicit-fallthrough"
    "-Wmisleading-indentation"
    "-Wno-missing-field-initializers"
    "-Wno-format-truncation"
    "-fno-math-errno"
    "-fno-trapping-math"
    "-Qunused-arguments"
    "-fno-common"
    "-Wno-unknown-pragmas"
    "-Wno-microsoft-enum-value"
    "-Wno-unused-function"
    "-Werror=thread-safety"
    "-ffunction-sections"
    "-fdata-sections"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    list(APPEND MESA_C_OPTIONS
        "-DHAVE_VC4"
    )
endif()
if(MESA_V3D)
    list(APPEND MESA_C_OPTIONS
        "-DHAVE_V3D"
        "-DHAVE_ZLIB"
        "-DHAVE_COMPRESSION"
    )
endif()
list(APPEND MESA_C_OPTIONS "-D__REACTOS__")
if(NOT MESA_LLVMPIPE)
    list(APPEND MESA_C_OPTIONS
        "-DHAVE_SOFTPIPE"
        "-DDRAW_LLVM_AVAILABLE=0"
        "-DAMD_LLVM_AVAILABLE=0"
    )
endif()
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    list(APPEND MESA_C_OPTIONS
        "-DHAVE_UINT128"
    )
endif()
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    list(APPEND MESA_C_OPTIONS
        "-DUSE_SSE41"
        "-DHAVE___BUILTIN_IA32_CLFLUSHOPT"
    )
endif()
if(MESA_PROFILE STREQUAL "i386")
    list(APPEND MESA_C_OPTIONS
        "-msse2"
        "-mfpmath=sse"
        "-mstackrealign"
    )
endif()
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    list(APPEND MESA_C_OPTIONS
        "-Wno-unused-variable"
        "-Wno-unused-but-set-variable"
    )
endif()
if(MESA_LLVMPIPE)
    list(APPEND MESA_C_OPTIONS
        "-DHAVE_LLVMPIPE"
        "-DMESA_LLVM_VERSION_STRING=\"${LLVM_PACKAGE_VERSION}\""
        "-DLLVM_IS_SHARED=0"
        "-DDRAW_LLVM_AVAILABLE=1"
        "-DAMD_LLVM_AVAILABLE=1"
    )
endif()
list(APPEND MESA_CXX_OPTIONS
    "-fvisibility=hidden"
    "-D_FILE_OFFSET_BITS=64"
    "-Wall"
    "-D__STDC_CONSTANT_MACROS"
    "-D__STDC_FORMAT_MACROS"
    "-D__STDC_LIMIT_MACROS"
    "-DPACKAGE_VERSION=\"${MESA_VERSION}\""
    "-DPACKAGE_BUGREPORT=\"https://gitlab.freedesktop.org/mesa/mesa/-/issues\""
    "-DHAVE_OPENGL=1"
    "-DHAVE_OPENGL_ES_1=0"
    "-DHAVE_OPENGL_ES_2=0"
    "-DHAVE_SWRAST"
    "-DMESA_SYSTEM_HAS_KMS_DRM=0"
    "-DVIDEO_CODEC_VC1DEC=0"
    "-DVIDEO_CODEC_H264DEC=0"
    "-DVIDEO_CODEC_H264ENC=0"
    "-DVIDEO_CODEC_H265DEC=0"
    "-DVIDEO_CODEC_H265ENC=0"
    "-DVIDEO_CODEC_AV1DEC=0"
    "-DVIDEO_CODEC_AV1ENC=0"
    "-DVIDEO_CODEC_VP9DEC=0"
    "-DVIDEO_CODEC_MPEG12DEC=0"
    "-DVIDEO_CODEC_JPEGDEC=0"
    "-DHAVE_WINDOWS_PLATFORM"
    "-DUSE_LIBGLVND=0"
    "-DHAVE_GFX_COMPUTE"
    "-DGLAPI_EXPORT_PROTO_ENTRY_POINTS=1"
    "-DALLOW_KCMP"
    "-DMESA_DEBUG=0"
    "-DHAVE___BUILTIN_BSWAP32"
    "-DHAVE___BUILTIN_BSWAP64"
    "-DHAVE___BUILTIN_CLZ"
    "-DHAVE___BUILTIN_CLZLL"
    "-DHAVE___BUILTIN_CTZ"
    "-DHAVE___BUILTIN_EXPECT"
    "-DHAVE___BUILTIN_FFS"
    "-DHAVE___BUILTIN_FFSLL"
    "-DHAVE___BUILTIN_POPCOUNT"
    "-DHAVE___BUILTIN_POPCOUNTLL"
    "-DHAVE___BUILTIN_UNREACHABLE"
    "-DHAVE___BUILTIN_TYPES_COMPATIBLE_P"
    "-DHAVE___BUILTIN_ADD_OVERFLOW"
    "-DHAVE_FUNC_ATTRIBUTE_CONST"
    "-DHAVE_FUNC_ATTRIBUTE_FLATTEN"
    "-DHAVE_FUNC_ATTRIBUTE_MALLOC"
    "-DHAVE_FUNC_ATTRIBUTE_PURE"
    "-DHAVE_FUNC_ATTRIBUTE_UNUSED"
    "-DHAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT"
    "-DHAVE_FUNC_ATTRIBUTE_WEAK"
    "-DHAVE_FUNC_ATTRIBUTE_FORMAT"
    "-DHAVE_FUNC_ATTRIBUTE_PACKED"
    "-DHAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL"
    "-DHAVE_FUNC_ATTRIBUTE_ALIAS"
    "-DHAVE_FUNC_ATTRIBUTE_NORETURN"
    "-DHAVE_FUNC_ATTRIBUTE_COLD"
    "-DHAVE_FUNC_ATTRIBUTE_VISIBILITY"
    "-D_WIN32_WINNT=0x0A00"
    "-DWINVER=0x0A00"
    "-D_GNU_SOURCE"
    "-DUSE_GCC_ATOMIC_BUILTINS"
    "-DHAS_SCHED_H"
    "-DHAVE_ENDIAN_H"
    "-DHAVE_CET_H"
    "-DHAVE_STRTOF"
    "-DHAVE_STRTOK_R"
    "-DHAVE_QSORT_S"
    "-DHAVE_STRUCT_TIMESPEC"
    "-DWIN32_LEAN_AND_MEAN"
    "-DGALLIVM_USE_ORCJIT=0"
    "-DTHREAD_SANITIZER=0"
    "-DHAVE_RENDERDOC_INTEGRATION=0"
    "-Werror=return-type"
    "-Werror=empty-body"
    "-Wmisleading-indentation"
    "-Wno-non-virtual-dtor"
    "-Wno-missing-field-initializers"
    "-Wno-format-truncation"
    "-fno-math-errno"
    "-fno-trapping-math"
    "-Qunused-arguments"
    "-Wno-unknown-pragmas"
    "-Wno-microsoft-enum-value"
    "-ffunction-sections"
    "-fdata-sections"
)
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec")
    list(APPEND MESA_CXX_OPTIONS
        "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST"
        "-DHAVE_VC4"
    )
endif()
if(MESA_V3D)
    list(APPEND MESA_CXX_OPTIONS
        "-DHAVE_V3D"
        "-DHAVE_ZLIB"
        "-DHAVE_COMPRESSION"
    )
endif()
list(APPEND MESA_CXX_OPTIONS "-D__REACTOS__")
if(NOT MESA_LLVMPIPE)
    list(APPEND MESA_CXX_OPTIONS
        "-DHAVE_SOFTPIPE"
        "-DDRAW_LLVM_AVAILABLE=0"
        "-DAMD_LLVM_AVAILABLE=0"
    )
endif()
if(MESA_PROFILE STREQUAL "arm64" OR MESA_PROFILE STREQUAL "arm64ec" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    list(APPEND MESA_CXX_OPTIONS
        "-DHAVE_UINT128"
    )
endif()
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64")
    list(APPEND MESA_CXX_OPTIONS
        "-DUSE_SSE41"
        "-DHAVE___BUILTIN_IA32_CLFLUSHOPT"
    )
endif()
if(MESA_PROFILE STREQUAL "i386")
    list(APPEND MESA_CXX_OPTIONS
        "-msse2"
        "-mfpmath=sse"
        "-mstackrealign"
    )
endif()
if(MESA_PROFILE STREQUAL "i386" OR MESA_PROFILE STREQUAL "amd64" OR MESA_PROFILE STREQUAL "llvm-amd64" OR MESA_PROFILE STREQUAL "llvm-arm64")
    list(APPEND MESA_CXX_OPTIONS
        "-Wno-unused-variable"
        "-Wno-unused-but-set-variable"
    )
endif()
if(MESA_LLVMPIPE)
    list(APPEND MESA_CXX_OPTIONS
        "-DHAVE_LLVMPIPE"
        "-DMESA_LLVM_VERSION_STRING=\"${LLVM_PACKAGE_VERSION}\""
        "-DLLVM_IS_SHARED=0"
        "-DDRAW_LLVM_AVAILABLE=1"
        "-DAMD_LLVM_AVAILABLE=1"
    )
endif()
