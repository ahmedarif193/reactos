
macro(require_program varname execname)
    # Respect an explicitly provided path, otherwise try to locate by name.
    # Also honor TOOLCHAIN_PATH if provided by the superbuild.
    # If a cached NOTFOUND is present, clear it and retry with our hints.
    if(DEFINED ${varname})
        if(${varname} MATCHES "-NOTFOUND$" OR (EXISTS "${${varname}}" ) STREQUAL "0")
            unset(${varname} CACHE)
            unset(${varname})
        endif()
    endif()

    if(NOT DEFINED ${varname} OR "${${varname}}" STREQUAL "")
        if(DEFINED TOOLCHAIN_PATH AND NOT "${TOOLCHAIN_PATH}" STREQUAL "")
            # Prefer an explicit toolchain path when provided
            find_program(${varname} NAMES ${execname} HINTS "${TOOLCHAIN_PATH}" PATHS "${TOOLCHAIN_PATH}" NO_DEFAULT_PATH)
        endif()
        if(NOT ${varname})
            find_program(${varname} NAMES ${execname})
        endif()
    endif()
    if(NOT ${varname})
        message(FATAL_ERROR "${execname} not found")
    endif()
endmacro()

# pass variables necessary for the toolchain (needed for try_compile)
# Include ARCH and the selected toolchain and explicit tool paths, so try_compile
# uses the same compilers and multilib settings as the parent build.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    ARCH
    MINGW_TOOLCHAIN_PREFIX
    MINGW_TOOLCHAIN_SUFFIX
    CMAKE_C_COMPILER
    CMAKE_CXX_COMPILER
    CMAKE_ASM_COMPILER
    CMAKE_MC_COMPILER
    CMAKE_RC_COMPILER
    CMAKE_DLLTOOL
    CMAKE_OBJCOPY
    CMAKE_AR
    CMAKE_RANLIB
    CMAKE_NM
    CMAKE_OBJDUMP
    CMAKE_READELF
    CMAKE_LINKER
    REACTOS_MULTILIB_I386)

# Choose the right MinGW toolchain prefix
if(NOT DEFINED MINGW_TOOLCHAIN_PREFIX)
    if(ARCH STREQUAL "i386")

        if(CMAKE_HOST_WIN32)
            set(MINGW_TOOLCHAIN_PREFIX "" CACHE STRING "MinGW Toolchain Prefix")
        else()
            set(MINGW_TOOLCHAIN_PREFIX "i686-w64-mingw32-" CACHE STRING "MinGW-W64 Toolchain Prefix")
        endif()

    elseif(ARCH STREQUAL "amd64")
        set(MINGW_TOOLCHAIN_PREFIX "x86_64-w64-mingw32-" CACHE STRING "MinGW Toolchain Prefix")
    elseif(ARCH STREQUAL "arm")
        set(MINGW_TOOLCHAIN_PREFIX "arm-mingw32ce-" CACHE STRING "MinGW Toolchain Prefix")
    elseif(ARCH STREQUAL "arm64")
        set(MINGW_TOOLCHAIN_PREFIX "aarch64-w64-mingw32-" CACHE STRING "MinGW Toolchain Prefix")
    endif()
endif()

if(NOT DEFINED MINGW_TOOLCHAIN_SUFFIX)
    set(MINGW_TOOLCHAIN_SUFFIX "" CACHE STRING "MinGW Toolchain Suffix")
endif()

# The name of the target operating system
set(CMAKE_SYSTEM_NAME Windows)

# Select the correct processor triple for the chosen architecture so CMake
# propagates the right values into try_compile() checks and generated files.
if(ARCH STREQUAL "i386")
    set(CMAKE_SYSTEM_PROCESSOR i686)
elseif(ARCH STREQUAL "amd64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
elseif(ARCH STREQUAL "arm")
    set(CMAKE_SYSTEM_PROCESSOR arm)
elseif(ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    set(CMAKE_SYSTEM_PROCESSOR i686)
endif()

# Which tools to use
require_program(CMAKE_C_COMPILER ${MINGW_TOOLCHAIN_PREFIX}gcc${MINGW_TOOLCHAIN_SUFFIX})
require_program(CMAKE_CXX_COMPILER ${MINGW_TOOLCHAIN_PREFIX}g++${MINGW_TOOLCHAIN_SUFFIX})
require_program(CMAKE_ASM_COMPILER ${MINGW_TOOLCHAIN_PREFIX}gcc${MINGW_TOOLCHAIN_SUFFIX})
set(CMAKE_ASM_COMPILER_ID "GNU")
require_program(CMAKE_MC_COMPILER ${MINGW_TOOLCHAIN_PREFIX}windmc)
require_program(CMAKE_RC_COMPILER ${MINGW_TOOLCHAIN_PREFIX}windres)
require_program(CMAKE_DLLTOOL ${MINGW_TOOLCHAIN_PREFIX}dlltool)
require_program(CMAKE_AR ${MINGW_TOOLCHAIN_PREFIX}ar)
require_program(CMAKE_RANLIB ${MINGW_TOOLCHAIN_PREFIX}ranlib)
require_program(CMAKE_OBJCOPY ${MINGW_TOOLCHAIN_PREFIX}objcopy)

# GNU binutils cannot convert between thin and normal archives in-place.
# Always rebuild static libraries from scratch to avoid thin-to-normal
# conversion attempts when an archive already exists.
set(_REACTOS_CREATE_STATIC_LIBRARY
    "<CMAKE_COMMAND> -E rm -f <TARGET> && <CMAKE_AR> rcs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_CREATE_STATIC_LIBRARY "${_REACTOS_CREATE_STATIC_LIBRARY}")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "${_REACTOS_CREATE_STATIC_LIBRARY}")
set(CMAKE_ASM_CREATE_STATIC_LIBRARY "${_REACTOS_CREATE_STATIC_LIBRARY}")
unset(_REACTOS_CREATE_STATIC_LIBRARY)

# Don't link with anything by default unless we say so
# Allow MinGW toolchain to provide its default startup/CRT libraries.
# (ReactOS modules will explicitly drop unwanted imports as needed.)

# This allows to have CMake test the compiler without linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Build position-independent code, required for ASLR
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC" CACHE STRING "C compiler flags")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC" CACHE STRING "C++ compiler flags")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -fPIC" CACHE STRING "ASM compiler flags")

set(CMAKE_SHARED_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -pie -Wl,--enable-auto-image-base,--disable-auto-import")

set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/overrides-gcc.cmake")

# Do not let CMake inject host C runtime/linker defaults; freestanding
# components (boot loaders, early kernel pieces, etc.) link everything
# explicitly and must not acquire user-mode DLL dependencies.
set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "Standard C Libraries" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "Standard C++ Libraries" FORCE)
if(ARCH STREQUAL "arm64")
    set(CMAKE_C_STANDARD_LIBRARIES "-latomic" CACHE STRING "Standard C Libraries" FORCE)
    set(CMAKE_CXX_STANDARD_LIBRARIES "-latomic" CACHE STRING "Standard C++ Libraries" FORCE)
endif()

# Get GCC version
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)

# Multilib support: allow building i386 code with an amd64 MinGW toolchain
# When ARCH is i386 but the selected toolchain prefix is x86_64, assume a multilib-enabled toolchain
# and inject the necessary -m32/pe-i386 flags so compilation, assembly, RC and linking target 32-bit.
if(ARCH STREQUAL "i386" AND MINGW_TOOLCHAIN_PREFIX MATCHES "^x86_64-w64-mingw32-")
    # Mark in cache for other CMake modules
    set(REACTOS_MULTILIB_I386 TRUE CACHE BOOL "Using x86_64 multilib toolchain for i386")
    message(STATUS "Toolchain: Using x86_64 MinGW (multilib) to build i386 (-m32)")
    # Force 32-bit codegen and link
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32" CACHE STRING "C compiler flags" FORCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32" CACHE STRING "C++ compiler flags" FORCE)
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -m32" CACHE STRING "ASM compiler flags" FORCE)
    # Ensure windres emits 32-bit COFF objects
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --target=pe-i386" CACHE STRING "RC compiler flags" FORCE)
endif()
