
# Use rossym (.rossym section) for debugging.
# NO_ROSSYM=OFF enables the rsym tool to generate .rossym sections from DWARF,
# which are embedded in the PE and can be loaded from memory by KDB.
# This is required because DWARF section names use COFF string table offsets
# (e.g., "/123") that are NOT mapped into memory when the PE is loaded,
# making it impossible to resolve them at runtime.
set(NO_ROSSYM OFF CACHE BOOL "Enable rossym generation from DWARF" FORCE)

if(DEFINED ENV{_ROSBE_ROSSCRIPTDIR})
    set(CMAKE_SYSROOT $ENV{_ROSBE_ROSSCRIPTDIR}/$ENV{ROS_ARCH})
endif()

# pass variables necessary for the toolchain (needed for try_compile)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    ARCH
    CLANG_VERSION
    MINGW_TOOLCHAIN_PREFIX
    MINGW_TOOLCHAIN_SUFFIX
    ROS_GNU_MINGW_TOOLCHAIN_PATH
    TOOLCHAIN_PATH
    TOOLCHAIN_PREFIX
    CMAKE_LINKER
    CMAKE_ASM_COMPILER
    CMAKE_MC_COMPILER
    CMAKE_RC_COMPILER
    CMAKE_DLLTOOL
    CMAKE_AR
    CMAKE_RANLIB
    CMAKE_NM
    CMAKE_OBJCOPY
    CMAKE_OBJDUMP
)

# The name of the target operating system
set(CMAKE_SYSTEM_NAME Windows)
# The processor we are targeting
if (ARCH STREQUAL "i386")
    set(CMAKE_SYSTEM_PROCESSOR i686)
elseif (ARCH STREQUAL "amd64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
elseif(ARCH STREQUAL "arm")
    set(CMAKE_SYSTEM_PROCESSOR arm)
elseif(ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    message(FATAL_ERROR "Unsupported ARCH: ${ARCH}")
endif()

if(DEFINED CLANG_VERSION AND NOT "${CLANG_VERSION}" STREQUAL "")
    set(CLANG_SUFFIX "-${CLANG_VERSION}")
else()
    set(CLANG_SUFFIX "")
endif()

set(_CLANG_C_COMPILER "clang${CLANG_SUFFIX}")
set(_CLANG_CXX_COMPILER "clang++${CLANG_SUFFIX}")
if(NOT CLANG_SUFFIX)
    set(_CLANG_C_COMPILER_NAME "${_CLANG_C_COMPILER}")
    set(_CLANG_CXX_COMPILER_NAME "${_CLANG_CXX_COMPILER}")
    if(DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "")
        if(EXISTS "${TOOLCHAIN_PATH}/${_CLANG_C_COMPILER_NAME}")
            set(_CLANG_C_COMPILER "${TOOLCHAIN_PATH}/${_CLANG_C_COMPILER_NAME}")
        endif()
        if(EXISTS "${TOOLCHAIN_PATH}/${_CLANG_CXX_COMPILER_NAME}")
            set(_CLANG_CXX_COMPILER "${TOOLCHAIN_PATH}/${_CLANG_CXX_COMPILER_NAME}")
        endif()
    endif()
    if(DEFINED ENV{TOOLCHAIN_PATH} AND NOT "$ENV{TOOLCHAIN_PATH}" STREQUAL "")
        if(EXISTS "$ENV{TOOLCHAIN_PATH}/${_CLANG_C_COMPILER_NAME}")
            set(_CLANG_C_COMPILER "$ENV{TOOLCHAIN_PATH}/${_CLANG_C_COMPILER_NAME}")
        endif()
        if(EXISTS "$ENV{TOOLCHAIN_PATH}/${_CLANG_CXX_COMPILER_NAME}")
            set(_CLANG_CXX_COMPILER "$ENV{TOOLCHAIN_PATH}/${_CLANG_CXX_COMPILER_NAME}")
        endif()
    endif()
    unset(_CLANG_C_COMPILER_NAME)
    unset(_CLANG_CXX_COMPILER_NAME)
endif()
if(CLANG_SUFFIX)
    set(_CLANG_C_COMPILER_NAME "${_CLANG_C_COMPILER}")
    set(_CLANG_CXX_COMPILER_NAME "${_CLANG_CXX_COMPILER}")

    set(_clang_c_found FALSE)
    if(DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "")
        if(EXISTS "${TOOLCHAIN_PATH}/${_CLANG_C_COMPILER_NAME}")
            set(_CLANG_C_COMPILER "${TOOLCHAIN_PATH}/${_CLANG_C_COMPILER_NAME}")
            set(_clang_c_found TRUE)
        endif()
    endif()
    if(NOT _clang_c_found)
        find_program(_CLANG_C_COMPILER_PATH NAMES "${_CLANG_C_COMPILER_NAME}")
        if(_CLANG_C_COMPILER_PATH)
            set(_CLANG_C_COMPILER "${_CLANG_C_COMPILER_PATH}")
            set(_clang_c_found TRUE)
        endif()
    endif()
    if(NOT _clang_c_found)
        message(FATAL_ERROR "clang${CLANG_SUFFIX} not found; install clang-${CLANG_VERSION} or set CLANG_VERSION accordingly")
    endif()
    unset(_clang_c_found)
    unset(_CLANG_C_COMPILER_PATH)

    set(_clang_cxx_found FALSE)
    if(DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "")
        if(EXISTS "${TOOLCHAIN_PATH}/${_CLANG_CXX_COMPILER_NAME}")
            set(_CLANG_CXX_COMPILER "${TOOLCHAIN_PATH}/${_CLANG_CXX_COMPILER_NAME}")
            set(_clang_cxx_found TRUE)
        endif()
    endif()
    if(NOT _clang_cxx_found)
        find_program(_CLANG_CXX_COMPILER_PATH NAMES "${_CLANG_CXX_COMPILER_NAME}")
        if(_CLANG_CXX_COMPILER_PATH)
            set(_CLANG_CXX_COMPILER "${_CLANG_CXX_COMPILER_PATH}")
            set(_clang_cxx_found TRUE)
        endif()
    endif()
    if(NOT _clang_cxx_found)
        message(FATAL_ERROR "clang++${CLANG_SUFFIX} not found; install clang-${CLANG_VERSION} or set CLANG_VERSION accordingly")
    endif()
    unset(_clang_cxx_found)
    unset(_CLANG_CXX_COMPILER_PATH)
    unset(_CLANG_C_COMPILER_NAME)
    unset(_CLANG_CXX_COMPILER_NAME)
endif()

# Which tools to use
set(triplet ${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32)

# Allow overriding the MinGW binutils prefix/suffix so multilib sub-builds can
# keep using the parent amd64 toolchain when targeting i386.
if(NOT DEFINED MINGW_TOOLCHAIN_PREFIX OR "${MINGW_TOOLCHAIN_PREFIX}" STREQUAL "")
    set(_clang_default_toolchain_prefix "")
    if(DEFINED TOOLCHAIN_PREFIX AND NOT "${TOOLCHAIN_PREFIX}" STREQUAL "")
        set(_clang_default_toolchain_prefix "${TOOLCHAIN_PREFIX}-")
    elseif(CMAKE_HOST_WIN32)
        set(_clang_default_toolchain_prefix "")
    else()
        set(_clang_default_toolchain_prefix "${triplet}-")
    endif()
    set(MINGW_TOOLCHAIN_PREFIX "${_clang_default_toolchain_prefix}" CACHE STRING "MinGW Toolchain Prefix")
    unset(_clang_default_toolchain_prefix)
endif()

if(NOT DEFINED MINGW_TOOLCHAIN_SUFFIX)
    set(MINGW_TOOLCHAIN_SUFFIX "" CACHE STRING "MinGW Toolchain Suffix")
endif()

set(_CLANG_USE_HOST_TOOLS FALSE)

set(_CLANG_MINGW_PREFIX "${MINGW_TOOLCHAIN_PREFIX}")
set(_CLANG_MINGW_SUFFIX "${MINGW_TOOLCHAIN_SUFFIX}")

set(_CLANG_MINGW_TOOL_HINT_DIRS)
macro(_clang_mingw_add_hint_dir _dir)
    if(NOT "${_dir}" STREQUAL "" AND IS_DIRECTORY "${_dir}")
        list(APPEND _CLANG_MINGW_TOOL_HINT_DIRS "${_dir}")
    endif()
endmacro()

macro(_clang_mingw_add_hint_from_tool _tool_var)
    if(DEFINED ${_tool_var} AND NOT "${${_tool_var}}" STREQUAL "")
        if(IS_ABSOLUTE "${${_tool_var}}")
            get_filename_component(_clang_mingw_hint_dir "${${_tool_var}}" DIRECTORY)
            _clang_mingw_add_hint_dir("${_clang_mingw_hint_dir}")
            unset(_clang_mingw_hint_dir)
        endif()
    endif()
endmacro()

if(NOT DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH OR "${ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
    if(DEFINED ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH} AND NOT "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        set(ROS_GNU_MINGW_TOOLCHAIN_PATH "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}")
    elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        string(REGEX REPLACE "-$" "" _clang_mingw_prefix_dir "${_CLANG_MINGW_PREFIX}")
        if(NOT _clang_mingw_prefix_dir STREQUAL "")
            set(_clang_mingw_default_gnu_toolchain_bin
                "$ENV{HOME}/mingw-toolchains/${_clang_mingw_prefix_dir}/bin")
            if(EXISTS "${_clang_mingw_default_gnu_toolchain_bin}")
                set(ROS_GNU_MINGW_TOOLCHAIN_PATH "${_clang_mingw_default_gnu_toolchain_bin}")
            endif()
            unset(_clang_mingw_default_gnu_toolchain_bin)
        endif()
        unset(_clang_mingw_prefix_dir)
    endif()
endif()
if(DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT "${ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
    set(ROS_GNU_MINGW_TOOLCHAIN_PATH "${ROS_GNU_MINGW_TOOLCHAIN_PATH}"
        CACHE PATH "Path to GNU MinGW toolchain (bin)")
endif()

if(DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "")
    _clang_mingw_add_hint_dir("${TOOLCHAIN_PATH}")
endif()
if(DEFINED ENV{TOOLCHAIN_PATH} AND NOT "$ENV{TOOLCHAIN_PATH}" STREQUAL "")
    _clang_mingw_add_hint_dir("$ENV{TOOLCHAIN_PATH}")
endif()
if(DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT "${ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
    _clang_mingw_add_hint_dir("${ROS_GNU_MINGW_TOOLCHAIN_PATH}")
endif()
if(DEFINED ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH} AND NOT "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
    _clang_mingw_add_hint_dir("$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}")
endif()

_clang_mingw_add_hint_from_tool(CMAKE_ASM_COMPILER)
_clang_mingw_add_hint_from_tool(CMAKE_MC_COMPILER)
_clang_mingw_add_hint_from_tool(CMAKE_RC_COMPILER)
_clang_mingw_add_hint_from_tool(CMAKE_DLLTOOL)
_clang_mingw_add_hint_from_tool(CMAKE_AR)
_clang_mingw_add_hint_from_tool(CMAKE_RANLIB)
_clang_mingw_add_hint_from_tool(CMAKE_NM)
_clang_mingw_add_hint_from_tool(CMAKE_OBJCOPY)
_clang_mingw_add_hint_from_tool(CMAKE_OBJDUMP)
list(REMOVE_DUPLICATES _CLANG_MINGW_TOOL_HINT_DIRS)

macro(_clang_mingw_refresh_hint_args)
    set(_CLANG_MINGW_TOOL_HINT_ARGS)
    if(_CLANG_MINGW_TOOL_HINT_DIRS)
        set(_CLANG_MINGW_TOOL_HINT_ARGS HINTS)
        list(APPEND _CLANG_MINGW_TOOL_HINT_ARGS ${_CLANG_MINGW_TOOL_HINT_DIRS})
    endif()
endmacro()
_clang_mingw_refresh_hint_args()

set(CMAKE_C_COMPILER ${_CLANG_C_COMPILER})
set(CMAKE_C_COMPILER_TARGET ${triplet})
set(CMAKE_CXX_COMPILER ${_CLANG_CXX_COMPILER})
set(CMAKE_CXX_COMPILER_TARGET ${triplet})
set(CMAKE_ASM_COMPILER_ID GNU)

# This allows to have CMake test the compiler without linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Mirror the GCC toolchain rule so thin-to-normal conversions are never needed.
set(_REACTOS_CREATE_STATIC_LIBRARY
    "<CMAKE_COMMAND> -E rm -f <TARGET> && <CMAKE_AR> rcs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_CREATE_STATIC_LIBRARY "${_REACTOS_CREATE_STATIC_LIBRARY}")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "${_REACTOS_CREATE_STATIC_LIBRARY}")
set(CMAKE_ASM_CREATE_STATIC_LIBRARY "${_REACTOS_CREATE_STATIC_LIBRARY}")
unset(_REACTOS_CREATE_STATIC_LIBRARY)

# Do not inject generic -lgcc/-lgcc_eh here: it can pull in host libgcc
# and clash with the MinGW toolchain libraries selected below. Our build
# system already links the correct libgcc/libgcc_eh via imported targets
# (see sdk/cmake/gcc.cmake). Leave the standard libraries empty.
set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "Standard C Libraries")
set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "Standard C++ Libraries")

# Linker selection for Clang cross-compilation
# - Use LLD (from llvm-mingw TOOLCHAIN_PATH) for i386 and amd64 for consistency across platforms
# - ARM64 may use GNU ld if needed to avoid "misaligned ldr/str offset" errors
set(_CLANG_MINGW_LINKER_NAME "${_CLANG_MINGW_PREFIX}ld${_CLANG_MINGW_SUFFIX}")

# ARM64: Special handling - may use GNU ld if ROS_GNU_MINGW_TOOLCHAIN_PATH is set
# This works around LLD issues with misaligned ARM64 relocations
set(_clang_use_gnu_ld_for_arm64 FALSE)
if(ARCH STREQUAL "arm64")
    if(DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT "${ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        set(_clang_use_gnu_ld_for_arm64 TRUE)
    elseif(DEFINED ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH} AND NOT "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        set(_clang_use_gnu_ld_for_arm64 TRUE)
    endif()
endif()

set(LD_EXECUTABLE "")

# ARM64 with GNU ld: Check ROS_GNU_MINGW_TOOLCHAIN_PATH first
if(_clang_use_gnu_ld_for_arm64)
    if(NOT LD_EXECUTABLE AND DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT "${ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        set(_clang_gnu_linker "${ROS_GNU_MINGW_TOOLCHAIN_PATH}/${_CLANG_MINGW_LINKER_NAME}")
        if(EXISTS "${_clang_gnu_linker}")
            set(LD_EXECUTABLE "${_clang_gnu_linker}")
        endif()
        unset(_clang_gnu_linker)
    elseif(NOT LD_EXECUTABLE AND DEFINED ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH} AND NOT "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        set(_clang_gnu_linker "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}/${_CLANG_MINGW_LINKER_NAME}")
        if(EXISTS "${_clang_gnu_linker}")
            set(LD_EXECUTABLE "${_clang_gnu_linker}")
        endif()
        unset(_clang_gnu_linker)
    endif()
endif()

# Primary linker location: Use TOOLCHAIN_PATH (contains llvm-mingw with LLD)
# This path is unified across Linux and Darwin - no hardcoded platform-specific paths
if(NOT LD_EXECUTABLE AND DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "")
    set(_clang_toolchain_linker "${TOOLCHAIN_PATH}/${_CLANG_MINGW_LINKER_NAME}")
    if(EXISTS "${_clang_toolchain_linker}")
        set(LD_EXECUTABLE "${_clang_toolchain_linker}")
    endif()
    unset(_clang_toolchain_linker)
endif()

# Fallback: Search in PATH
if(NOT LD_EXECUTABLE)
    find_program(LD_EXECUTABLE
        NAMES ${_CLANG_MINGW_LINKER_NAME}
        ${_CLANG_MINGW_TOOL_HINT_ARGS})
    if(NOT LD_EXECUTABLE)
        message(FATAL_ERROR "Unable to find ${_CLANG_MINGW_LINKER_NAME} in TOOLCHAIN_PATH (${TOOLCHAIN_PATH}) or system PATH")
    endif()
endif()

unset(_clang_use_gnu_ld_for_arm64)
message(STATUS "Using linker ${LD_EXECUTABLE}")
set(CMAKE_LINKER "${LD_EXECUTABLE}" CACHE FILEPATH "Linker executable" FORCE)
execute_process(COMMAND ${CMAKE_LINKER} --version
    OUTPUT_VARIABLE _MINGW_LD_VERSION
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(_MINGW_LD_VERSION MATCHES "LLD")
    set(MINGW_LINKER_IS_LLD TRUE CACHE BOOL "MinGW linker is lld" FORCE)
else()
    set(MINGW_LINKER_IS_LLD FALSE CACHE BOOL "MinGW linker is lld" FORCE)
endif()
unset(_MINGW_LD_VERSION)

set(_CLANG_MINGW_LINKER_FLAG_VARS
    CMAKE_SHARED_LINKER_FLAGS_INIT
    CMAKE_MODULE_LINKER_FLAGS_INIT
    CMAKE_EXE_LINKER_FLAGS_INIT)
foreach(_clang_flag_var ${_CLANG_MINGW_LINKER_FLAG_VARS})
    set(${_clang_flag_var} "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import")
endforeach()

get_filename_component(_CLANG_MINGW_TOOL_DIR "${LD_EXECUTABLE}" DIRECTORY)
if(_CLANG_MINGW_TOOL_DIR)
    _clang_mingw_add_hint_dir("${_CLANG_MINGW_TOOL_DIR}")
    list(REMOVE_DUPLICATES _CLANG_MINGW_TOOL_HINT_DIRS)
    _clang_mingw_refresh_hint_args()
endif()
if(_CLANG_MINGW_TOOL_DIR)
    foreach(_clang_flag_var ${_CLANG_MINGW_LINKER_FLAG_VARS})
        string(APPEND ${_clang_flag_var} " -B\"${_CLANG_MINGW_TOOL_DIR}\"")
    endforeach()
endif()
set(_CLANG_MINGW_FUSE_LD_ARG "${LD_EXECUTABLE}")
foreach(_clang_flag_var ${_CLANG_MINGW_LINKER_FLAG_VARS})
    string(APPEND ${_clang_flag_var} " -fuse-ld=\"${_CLANG_MINGW_FUSE_LD_ARG}\"")
endforeach()
unset(_CLANG_MINGW_FUSE_LD_ARG)
unset(_CLANG_MINGW_LINKER_FLAG_VARS)

macro(_clang_mingw_require_tool _out_var _tool_name)
    set(_clang_mingw_tool_names "${_CLANG_MINGW_PREFIX}${_tool_name}${_CLANG_MINGW_SUFFIX}")
    if(_CLANG_USE_HOST_TOOLS)
        list(APPEND _clang_mingw_tool_names "${_tool_name}")
    endif()
    find_program(${_out_var}
        NAMES ${_clang_mingw_tool_names}
        ${_CLANG_MINGW_TOOL_HINT_ARGS})
    if(NOT ${_out_var})
        message(FATAL_ERROR "Unable to find ${_CLANG_MINGW_PREFIX}${_tool_name}${_CLANG_MINGW_SUFFIX}")
    endif()
    unset(_clang_mingw_tool_names)
endmacro()

if(_CLANG_USE_HOST_TOOLS)
    set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER} CACHE FILEPATH "Clang used for assembly" FORCE)
    set(CMAKE_ASM_COMPILER_TARGET ${triplet})
    if(NOT CMAKE_ASM_FLAGS MATCHES "--target=")
        set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} --target=${triplet}" CACHE STRING "ASM compiler flags" FORCE)
    endif()
else()
    _clang_mingw_require_tool(_CLANG_MINGW_GCC "gcc")
    set(CMAKE_ASM_COMPILER ${_CLANG_MINGW_GCC} CACHE FILEPATH "MinGW GCC used for assembly" FORCE)
endif()
_clang_mingw_require_tool(_CLANG_MINGW_WINDMC "windmc")
set(CMAKE_MC_COMPILER ${_CLANG_MINGW_WINDMC} CACHE FILEPATH "MinGW message compiler" FORCE)
_clang_mingw_require_tool(_CLANG_MINGW_WINDRES "windres")
set(CMAKE_RC_COMPILER ${_CLANG_MINGW_WINDRES} CACHE FILEPATH "MinGW resource compiler" FORCE)
# Prefer GNU binutils dlltool when available; llvm-dlltool lacks --kill-at/--output-lib
# and generates import stubs with IMAGE_REL_ARM64_ADDR32 relocations that cause overflow.
# For ARM64, we MUST use binutils dlltool from ROS_GNU_MINGW_TOOLCHAIN_PATH because
# it generates proper ADRP+LDR sequences with PAGE relocations that work across the
# full 64-bit address space.
if(ARCH STREQUAL "arm64")
    # ARM64 requires binutils dlltool - llvm-dlltool generates short-form imports
    # that use IMAGE_REL_ARM64_ADDR32 relocations causing linker overflow errors
    if(DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT "${ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        set(_clang_arm64_binutils_dlltool "${ROS_GNU_MINGW_TOOLCHAIN_PATH}/${_CLANG_MINGW_PREFIX}dlltool${_CLANG_MINGW_SUFFIX}")
        if(EXISTS "${_clang_arm64_binutils_dlltool}")
            set(CMAKE_DLLTOOL "${_clang_arm64_binutils_dlltool}" CACHE FILEPATH "MinGW dlltool" FORCE)
            message(STATUS "ARM64: Using binutils dlltool from ${_clang_arm64_binutils_dlltool}")
        else()
            _clang_mingw_require_tool(_CLANG_MINGW_DLLTOOL "dlltool")
            set(CMAKE_DLLTOOL ${_CLANG_MINGW_DLLTOOL} CACHE FILEPATH "MinGW dlltool" FORCE)
            message(WARNING "ARM64: Binutils dlltool not found at ${_clang_arm64_binutils_dlltool}, using ${_CLANG_MINGW_DLLTOOL}")
        endif()
        unset(_clang_arm64_binutils_dlltool)
    else()
        _clang_mingw_require_tool(_CLANG_MINGW_DLLTOOL "dlltool")
        set(CMAKE_DLLTOOL ${_CLANG_MINGW_DLLTOOL} CACHE FILEPATH "MinGW dlltool" FORCE)
        message(WARNING "ARM64: ROS_GNU_MINGW_TOOLCHAIN_PATH not set, using ${_CLANG_MINGW_DLLTOOL}")
    endif()
else()
    _clang_mingw_require_tool(_CLANG_MINGW_DLLTOOL "dlltool")
    set(CMAKE_DLLTOOL ${_CLANG_MINGW_DLLTOOL} CACHE FILEPATH "MinGW dlltool" FORCE)
endif()
_clang_mingw_require_tool(_CLANG_MINGW_AR "ar")
# Always use binutils from the MinGW toolchain for archive creation.
# This avoids incompatibilities with llvm-dlltool option handling.
set(CMAKE_AR ${_CLANG_MINGW_AR} CACHE FILEPATH "MinGW archiver" FORCE)
_clang_mingw_require_tool(_CLANG_MINGW_OBJCOPY "objcopy")
set(CMAKE_OBJCOPY ${_CLANG_MINGW_OBJCOPY} CACHE FILEPATH "MinGW objcopy" FORCE)
_clang_mingw_require_tool(_CLANG_MINGW_OBJDUMP "objdump")
set(CMAKE_OBJDUMP ${_CLANG_MINGW_OBJDUMP} CACHE FILEPATH "MinGW objdump" FORCE)
_clang_mingw_require_tool(_CLANG_MINGW_NM "nm")
set(CMAKE_NM ${_CLANG_MINGW_NM} CACHE FILEPATH "MinGW nm" FORCE)
_clang_mingw_require_tool(_CLANG_MINGW_RANLIB "ranlib")
set(CMAKE_RANLIB ${_CLANG_MINGW_RANLIB} CACHE FILEPATH "MinGW ranlib" FORCE)

set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/overrides-gcc.cmake")

if(ARCH STREQUAL "i386" AND MINGW_TOOLCHAIN_PREFIX MATCHES "^x86_64-w64-mingw32-")
    set(REACTOS_MULTILIB_I386 TRUE CACHE BOOL "Using x86_64 multilib toolchain for i386")
    message(STATUS "Toolchain: Using x86_64 MinGW (multilib) to build i386 (-m32)")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32" CACHE STRING "C compiler flags" FORCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32" CACHE STRING "C++ compiler flags" FORCE)
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -m32" CACHE STRING "ASM compiler flags" FORCE)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --target=pe-i386" CACHE STRING "RC compiler flags" FORCE)
endif()
