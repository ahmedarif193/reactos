
macro(require_program varname execname)
    find_program(${varname} ${execname})
    if(NOT ${varname})
        message(FATAL_ERROR "${execname} not found")
    endif()
endmacro()

# pass variables necessary for the toolchain (needed for try_compile)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARCH)

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
set(CMAKE_SYSTEM_PROCESSOR i686)

# Which tools to use
# FIXME TODO: ARM64 toolchain requires full path
if(ARCH STREQUAL "arm64")
    set(CMAKE_C_COMPILER /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}gcc${MINGW_TOOLCHAIN_SUFFIX})
    set(CMAKE_CXX_COMPILER /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}g++${MINGW_TOOLCHAIN_SUFFIX})
    set(CMAKE_ASM_COMPILER /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}gcc${MINGW_TOOLCHAIN_SUFFIX})
else()
    require_program(CMAKE_C_COMPILER ${MINGW_TOOLCHAIN_PREFIX}gcc${MINGW_TOOLCHAIN_SUFFIX})
    require_program(CMAKE_CXX_COMPILER ${MINGW_TOOLCHAIN_PREFIX}g++${MINGW_TOOLCHAIN_SUFFIX})
    require_program(CMAKE_ASM_COMPILER ${MINGW_TOOLCHAIN_PREFIX}gcc${MINGW_TOOLCHAIN_SUFFIX})
endif()
set(CMAKE_ASM_COMPILER_ID "GNU")
# FIXME TODO: ARM64 toolchain requires full path
if(ARCH STREQUAL "arm64")
    set(CMAKE_MC_COMPILER /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}windmc)
    set(CMAKE_RC_COMPILER /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}windres)
    set(CMAKE_DLLTOOL /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}dlltool)
    set(CMAKE_AR /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}ar)
    set(CMAKE_RANLIB /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}ranlib)
    set(CMAKE_OBJCOPY /home/ahmed/x-tools/aarch64-w64-mingw32/bin/${MINGW_TOOLCHAIN_PREFIX}objcopy)
else()
    require_program(CMAKE_MC_COMPILER ${MINGW_TOOLCHAIN_PREFIX}windmc)
    require_program(CMAKE_RC_COMPILER ${MINGW_TOOLCHAIN_PREFIX}windres)
    require_program(CMAKE_DLLTOOL ${MINGW_TOOLCHAIN_PREFIX}dlltool)
    require_program(CMAKE_AR ${MINGW_TOOLCHAIN_PREFIX}ar)
    require_program(CMAKE_RANLIB ${MINGW_TOOLCHAIN_PREFIX}ranlib)
    require_program(CMAKE_OBJCOPY ${MINGW_TOOLCHAIN_PREFIX}objcopy)
endif()

set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> cr <TARGET> <LINK_FLAGS> <OBJECTS>" "<CMAKE_RANLIB> <TARGET>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY ${CMAKE_C_CREATE_STATIC_LIBRARY})
set(CMAKE_ASM_CREATE_STATIC_LIBRARY ${CMAKE_C_CREATE_STATIC_LIBRARY})

# Don't link with anything by default unless we say so
# FIXME TODO: ARM64 toolchain is Clang-based, doesn't have libgcc
if(ARCH STREQUAL "arm64")
    set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "Standard C Libraries")
    set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "Standard C++ Libraries")
else()
    set(CMAKE_C_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C Libraries")
    #MARK_AS_ADVANCED(CLEAR CMAKE_CXX_STANDARD_LIBRARIES)
    set(CMAKE_CXX_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C++ Libraries")
endif()

# This allows to have CMake test the compiler without linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Set compiler target for ARM64
if(ARCH STREQUAL "arm64")
    set(CMAKE_C_COMPILER_TARGET "aarch64-w64-mingw32")
    set(CMAKE_CXX_COMPILER_TARGET "aarch64-w64-mingw32")
endif()

set(CMAKE_SHARED_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import")

set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/overrides-gcc.cmake")

# Get GCC version
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
