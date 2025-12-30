
# Show a note about ccache build
if(ENABLE_CCACHE)
    message("-- Enabling ccache build - done")
    set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES OFF)
    set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES OFF)
endif()

# PDB style debug info
if(NOT DEFINED SEPARATE_DBG)
    set(SEPARATE_DBG FALSE)
endif()

# Dwarf-based builds toggle (no rsym)
# Expose NO_ROSSYM in the cache so it can be toggled explicitly.
if(NOT DEFINED NO_ROSSYM)
    set(NO_ROSSYM OFF CACHE BOOL "Disable rossym (.rossym) generation; rely on DWARF only")
endif()

# Force-disable rossym in configurations where it is unsupported or undesired.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(NO_ROSSYM ON CACHE BOOL "Disable rossym (.rossym) generation; rely on DWARF only" FORCE)
elseif(NOT ARCH STREQUAL "i386" AND NOT ARCH STREQUAL "amd64")
    set(NO_ROSSYM ON CACHE BOOL "Disable rossym (.rossym) generation; rely on DWARF only" FORCE)
endif()

if(NOT DEFINED USE_PSEH3)
    set(USE_PSEH3 1)
endif()

# PSEH3 is x86-only; disable it for other architectures.
if(NOT ARCH STREQUAL "i386")
    set(USE_PSEH3 0 CACHE BOOL "PSEH3 is only supported on i386" FORCE)
endif()

if(USE_PSEH3)
    add_definitions(-D_USE_PSEH3=1)
elseif(NOT ARCH STREQUAL "i386")
    add_compile_options(-U_USE_PSEH3)
endif()

if(NOT DEFINED USE_DUMMY_PSEH)
    set(USE_DUMMY_PSEH 0)
endif()

if(USE_DUMMY_PSEH)
    add_definitions(-D_USE_DUMMY_PSEH=1)
endif()

# Mirror the tool hint handling from the Clang toolchain so consumers can
# override the MinGW prefix/suffix once and have both toolchains honor it.
if(NOT DEFINED MINGW_TOOLCHAIN_PREFIX)
    set(_mingw_triplet "")
    if(DEFINED CMAKE_CXX_COMPILER_TARGET AND NOT "${CMAKE_CXX_COMPILER_TARGET}" STREQUAL "")
        set(_mingw_triplet "${CMAKE_CXX_COMPILER_TARGET}")
    elseif(ARCH STREQUAL "i386")
        set(_mingw_triplet "i686-w64-mingw32")
    elseif(ARCH STREQUAL "amd64")
        set(_mingw_triplet "x86_64-w64-mingw32")
    elseif(ARCH STREQUAL "arm")
        set(_mingw_triplet "armv7-w64-mingw32")
    elseif(ARCH STREQUAL "arm64")
        set(_mingw_triplet "aarch64-w64-mingw32")
    endif()
    set(_mingw_default_prefix "")
    if(DEFINED TOOLCHAIN_PREFIX AND NOT "${TOOLCHAIN_PREFIX}" STREQUAL "")
        set(_mingw_default_prefix "${TOOLCHAIN_PREFIX}-")
    elseif(NOT "${_mingw_triplet}" STREQUAL "")
        set(_mingw_default_prefix "${_mingw_triplet}-")
    endif()
    set(MINGW_TOOLCHAIN_PREFIX "${_mingw_default_prefix}" CACHE STRING "MinGW Toolchain Prefix")
    unset(_mingw_default_prefix)
    unset(_mingw_triplet)
endif()

if(NOT DEFINED MINGW_TOOLCHAIN_SUFFIX)
    set(MINGW_TOOLCHAIN_SUFFIX "" CACHE STRING "MinGW Toolchain Suffix")
endif()

if(STACK_PROTECTOR)
    add_compile_options(-fstack-protector-strong)
endif()

# Compiler Core
# note: -fno-common is default since GCC 10
add_compile_options(-pipe -fms-extensions -fno-strict-aliasing -fno-common)

# A long double is 64 bits on Windows; enforce this where the toolchain defaults
# to wider quad-precision to avoid pulling in libgcc tf helpers.
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_compile_options(-mlong-double-64)
endif()

# Prevent GCC from searching any of the default directories.
# The case for C++ is handled through the reactos_c++ INTERFACE library
add_compile_options("$<$<NOT:$<COMPILE_LANGUAGE:CXX>>:-nostdinc>")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fgnu89-inline>)
    add_compile_options("-Wno-unknown-pragmas")
    add_compile_options(-fno-aggressive-loop-optimizations)
    if (DBG)
        add_compile_options("$<$<COMPILE_LANGUAGE:C>:-Wold-style-declaration>")
    endif()

    # Disable all math intrinsics. The reason is that these are implicitly declared
    # extern by GCC, which causes inline functions to generate global symbols.
    # And since GCC is retarded, these symbols are not marked as weak, so they
    # conflict with each other in multiple compilation units.
    add_compile_options(-fno-builtin-acosf)
    add_compile_options(-fno-builtin-acosl)
    add_compile_options(-fno-builtin-asinf)
    add_compile_options(-fno-builtin-asinl)
    add_compile_options(-fno-builtin-atan2f)
    add_compile_options(-fno-builtin-atan2l)
    add_compile_options(-fno-builtin-atanf)
    add_compile_options(-fno-builtin-atanl)
    add_compile_options(-fno-builtin-ceilf)
    add_compile_options(-fno-builtin-ceill)
    add_compile_options(-fno-builtin-coshf)
    add_compile_options(-fno-builtin-coshl)
    add_compile_options(-fno-builtin-cosf)
    add_compile_options(-fno-builtin-cosl)
    add_compile_options(-fno-builtin-expf)
    add_compile_options(-fno-builtin-expl)
    add_compile_options(-fno-builtin-fabsf)
    add_compile_options(-fno-builtin-fabsl)
    add_compile_options(-fno-builtin-floorf)
    add_compile_options(-fno-builtin-floorl)
    add_compile_options(-fno-builtin-fmodf)
    add_compile_options(-fno-builtin-fmodl)
    add_compile_options(-fno-builtin-frexpf)
    add_compile_options(-fno-builtin-frexpl)
    add_compile_options(-fno-builtin-hypotf)
    add_compile_options(-fno-builtin-hypotl)
    add_compile_options(-fno-builtin-ldexpf)
    add_compile_options(-fno-builtin-ldexpl)
    add_compile_options(-fno-builtin-logf)
    add_compile_options(-fno-builtin-logl)
    add_compile_options(-fno-builtin-log10f)
    add_compile_options(-fno-builtin-log10l)
    add_compile_options(-fno-builtin-modff)
    add_compile_options(-fno-builtin-modfl)
    add_compile_options(-fno-builtin-powf)
    add_compile_options(-fno-builtin-powl)
    add_compile_options(-fno-builtin-sinhf)
    add_compile_options(-fno-builtin-sinhl)
    add_compile_options(-fno-builtin-sinf)
    add_compile_options(-fno-builtin-sinl)
    add_compile_options(-fno-builtin-sqrtf)
    add_compile_options(-fno-builtin-sqrtl)
    add_compile_options(-fno-builtin-tanhf)
    add_compile_options(-fno-builtin-tanhl)
    add_compile_options(-fno-builtin-tanf)
    add_compile_options(-fno-builtin-tanl)
    add_compile_options(-fno-builtin-feraiseexcept)
    add_compile_options(-fno-builtin-feupdateenv)

    if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 11)
        add_compile_options(-fno-builtin-ceil)
        add_compile_options(-fno-builtin-ceilf)
        add_compile_options(-fno-builtin-cos)
        add_compile_options(-fno-builtin-floor)
        add_compile_options(-fno-builtin-floorf)
        add_compile_options(-fno-builtin-pow)
        add_compile_options(-fno-builtin-sin)
        add_compile_options(-fno-builtin-sincos)
        add_compile_options(-fno-builtin-sqrt)
        add_compile_options(-fno-builtin-sqrtf)
    endif()
    if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
        add_compile_options(-fno-builtin-erf)
        add_compile_options(-fno-builtin-erff)
    endif()

elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-Wno-microsoft>")
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fgnu89-inline>)
    add_compile_options(-Wno-pragma-pack)
    add_compile_options(-fno-associative-math)
    if(ARCH STREQUAL "i386")
        add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fsjlj-exceptions>")
    endif()

    if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0)
        # disable "libcall optimization"
        # see https://mudongliang.github.io/2020/12/02/undefined-reference-to-stpcpy.html
        add_compile_options(-fno-builtin-stpcpy)
    endif()

    set(CMAKE_LINK_DEF_FILE_FLAG "")
    set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
    set(CMAKE_LINK_LIBRARY_SUFFIX "")
    set(CMAKE_CREATE_WIN32_EXE "")
    set(CMAKE_C_COMPILE_OPTIONS_PIC "")
    set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
    set(CMAKE_C_COMPILE_OPTIONS_PIE "")
    set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")
    set(CMAKE_ASM_FLAGS_DEBUG "")
    set(CMAKE_C_FLAGS_DEBUG "")
    set(CMAKE_CXX_FLAGS_DEBUG "")
endif()

# Debugging
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(SEPARATE_DBG)
        add_compile_options(-gdwarf-2 -ggdb)
    else()
        add_compile_options(-gdwarf-2 -gstrict-dwarf)
        if(NOT CMAKE_C_COMPILER_ID STREQUAL Clang)
            add_compile_options(-femit-struct-debug-detailed=none -feliminate-unused-debug-symbols)
        endif()
    endif()
endif()

# Tuning
add_compile_options(-march=${OARCH} -mtune=${TUNE})
if(ARCH STREQUAL "arm64")
    # Avoid GCC's out-of-line atomic helpers (__aarch64_*), which are not
    # available in the freestanding kernel/driver environment.
    add_compile_options(-mno-outline-atomics)
endif()

# Warnings, errors
# Only treat warnings as errors for Debug builds (GCC only)
if((CMAKE_BUILD_TYPE STREQUAL "Debug") AND (NOT CMAKE_C_COMPILER_ID STREQUAL Clang))
    add_compile_options(-Werror)
endif()

add_compile_options(-Wall -Wpointer-arith -Werror=maybe-uninitialized)

# Disable some overzealous warnings
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-unknown-warning-option)
endif()

add_compile_options(
    -Wno-char-subscripts
    -Wno-multichar
    -Wno-unused-value
    -Wno-unused-const-variable
    -Wno-unused-local-typedefs
    -Wno-deprecated
    -Wno-unused-result
    -Wno-format
    -Wno-maybe-uninitialized
)

if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    add_compile_options(-Wno-format)
elseif(ARCH STREQUAL "arm")
    add_compile_options(-Wno-attributes)
endif()

# Optimizations
# FIXME: Revisit this to see if we even need these levels
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
    add_compile_options(-Wno-unused-variable)
    add_compile_options(-Wno-unused-but-set-variable)
else()
    if(OPTIMIZE STREQUAL "1")
        add_compile_options(-Os)
        if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-ftracer)
        endif()
    elseif(OPTIMIZE STREQUAL "2")
        add_compile_options(-Os)
    elseif(OPTIMIZE STREQUAL "3")
        add_compile_options(-Og)
    elseif(OPTIMIZE STREQUAL "4")
        add_compile_options(-O1)
    elseif(OPTIMIZE STREQUAL "5")
        add_compile_options(-O2)
    elseif(OPTIMIZE STREQUAL "6")
        add_compile_options(-O3)
    elseif(OPTIMIZE STREQUAL "7")
        add_compile_options(-Ofast)
    endif()
endif()

# Link-time code generation
if(LTCG)
    add_compile_options(-flto -fno-fat-lto-objects)
endif()

if(ARCH STREQUAL "i386")
    add_compile_options(-fno-optimize-sibling-calls -fno-omit-frame-pointer -mstackrealign)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-mpreferred-stack-boundary=3 -fno-set-stack-executable)
    endif()
    # FIXME: this doesn't work. CMAKE_BUILD_TYPE is always "Debug"
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-momit-leaf-frame-pointer)
    endif()
elseif(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-mpreferred-stack-boundary=4)
    endif()
    add_compile_options(-Wno-error)
endif()

# Other
if(ARCH STREQUAL "amd64")
    add_compile_options(-mcx16) # Generate CMPXCHG16
    add_definitions(-U_X86_ -UWIN32)
elseif(ARCH STREQUAL "arm")
    add_definitions(-U_UNICODE -UUNICODE)
    add_definitions(-D__MSVCRT__) # DUBIOUS
endif()

# Fix build with GLIBCXX + our c++ headers
add_definitions(-D_GLIBCXX_HAVE_BROKEN_VSWPRINTF -D_GLIBCXX_HAVE_QUICK_EXIT=0 -D_GLIBCXX_HAVE_AT_QUICK_EXIT=0)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include reactos/quick_exit_compat.h")
add_compile_definitions("$<$<COMPILE_LANGUAGE:CXX>:__STDC_CONSTANT_MACROS>"
                       "$<$<COMPILE_LANGUAGE:CXX>:__STDC_LIMIT_MACROS>")

# Fix build with UCRT headers
add_definitions(-D_CRT_SUPPRESS_RESTRICT)

# Alternative arch name
if(ARCH STREQUAL "amd64")
    set(ARCH2 x86_64)
else()
    set(ARCH2 ${ARCH})
endif()

if(SEPARATE_DBG)
    # PDB style debug puts all dwarf debug info in a separate dbg file
    message(STATUS "Building separate debug symbols")
    file(MAKE_DIRECTORY ${REACTOS_BINARY_DIR}/symbols)
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        # Those variables seems to be set but empty in newer CMake versions
        # and Ninja generator relies on them to generate PDB name, so unset them.
        unset(MSVC_C_ARCHITECTURE_ID)
        unset(MSVC_CXX_ARCHITECTURE_ID)
        set(CMAKE_DEBUG_SYMBOL_SUFFIX "")
        set(SYMBOL_FILE <TARGET_PDB>)
    else()
        set(SYMBOL_FILE <TARGET>)
    endif()

    if (NOT NO_ROSSYM)
        get_target_property(RSYM native-rsym IMPORTED_LOCATION)
        set(strip_debug "${RSYM} -s ${REACTOS_SOURCE_DIR} <TARGET> <TARGET>")
    else()
        set(strip_debug "${CMAKE_STRIP} --strip-debug <TARGET>")
    endif()

    set(CMAKE_C_LINK_EXECUTABLE
        "<CMAKE_C_COMPILER> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
        "${CMAKE_STRIP} --only-keep-debug <TARGET> -o ${REACTOS_BINARY_DIR}/symbols/${SYMBOL_FILE}"
        ${strip_debug})
    set(CMAKE_CXX_LINK_EXECUTABLE
        "<CMAKE_CXX_COMPILER> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
        "${CMAKE_STRIP} --only-keep-debug <TARGET> -o ${REACTOS_BINARY_DIR}/symbols/${SYMBOL_FILE}"
        ${strip_debug})
    set(CMAKE_C_CREATE_SHARED_LIBRARY
        "<CMAKE_C_COMPILER> <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>"
        "${CMAKE_STRIP} --only-keep-debug <TARGET> -o ${REACTOS_BINARY_DIR}/symbols/${SYMBOL_FILE}"
        ${strip_debug})
    set(CMAKE_CXX_CREATE_SHARED_LIBRARY
        "<CMAKE_CXX_COMPILER> <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>"
        "${CMAKE_STRIP} --only-keep-debug <TARGET> -o ${REACTOS_BINARY_DIR}/symbols/${SYMBOL_FILE}"
        ${strip_debug})
    set(CMAKE_RC_CREATE_SHARED_LIBRARY
        "<CMAKE_C_COMPILER> <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>"
        "${CMAKE_STRIP} --only-keep-debug <TARGET> -o ${REACTOS_BINARY_DIR}/symbols/${SYMBOL_FILE}"
        ${strip_debug})
elseif(NO_ROSSYM)
    # Dwarf-based build
    message(STATUS "Generating a dwarf-based build (no rsym)")
    # Use --start-group/--end-group to resolve circular/static lib dependencies
    set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> ${CMAKE_C_FLAGS} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
    set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> ${CMAKE_CXX_FLAGS} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
    set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_C_COMPILER> ${CMAKE_C_FLAGS} <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
    set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_CXX_COMPILER> ${CMAKE_CXX_FLAGS} <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> -o <TARGET> <OBJECTS> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
    set(CMAKE_RC_CREATE_SHARED_LIBRARY "<CMAKE_C_COMPILER> ${CMAKE_C_FLAGS} <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
else()
    # Normal rsym build
    get_target_property(RSYM native-rsym IMPORTED_LOCATION)

    set(CMAKE_C_LINK_EXECUTABLE
        "<CMAKE_C_COMPILER> ${CMAKE_C_FLAGS} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
        "${RSYM} -s ${REACTOS_SOURCE_DIR} <TARGET> <TARGET>")
    set(CMAKE_CXX_LINK_EXECUTABLE
        "<CMAKE_CXX_COMPILER> ${CMAKE_CXX_FLAGS} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
        "${RSYM} -s ${REACTOS_SOURCE_DIR} <TARGET> <TARGET>")
    set(CMAKE_C_CREATE_SHARED_LIBRARY
        "<CMAKE_C_COMPILER> ${CMAKE_C_FLAGS} <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
        "${RSYM} -s ${REACTOS_SOURCE_DIR} <TARGET> <TARGET>")
    set(CMAKE_CXX_CREATE_SHARED_LIBRARY
        "<CMAKE_CXX_COMPILER> ${CMAKE_CXX_FLAGS} <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> -o <TARGET> <OBJECTS> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
        "${RSYM} -s ${REACTOS_SOURCE_DIR} <TARGET> <TARGET>")
    set(CMAKE_RC_CREATE_SHARED_LIBRARY
        "<CMAKE_C_COMPILER> ${CMAKE_C_FLAGS} <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
endif()

set(CMAKE_C_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
set(CMAKE_CXX_CREATE_SHARED_MODULE ${CMAKE_CXX_CREATE_SHARED_LIBRARY})
set(CMAKE_RC_CREATE_SHARED_MODULE ${CMAKE_RC_CREATE_SHARED_LIBRARY})

if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    # Workaround for binutils linker segfault on amd64 - disable auto-image-base
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_INIT} -Wl,--disable-stdcall-fixup,--gc-sections")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_INIT} -Wl,--disable-stdcall-fixup")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS_INIT} -Wl,--disable-stdcall-fixup")
else()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_INIT} -Wl,--disable-stdcall-fixup,--gc-sections")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_INIT} -Wl,--disable-stdcall-fixup")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS_INIT} -Wl,--disable-stdcall-fixup")
endif()

set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> ${_compress_debug_sections_flag} <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
# FIXME: Once the GCC toolchain bugs are fixed, add _compress_debug_sections_flag to CXX too
set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> ${_compress_debug_sections_flag} -x assembler-with-cpp -o <OBJECT> -I${REACTOS_SOURCE_DIR}/sdk/include/asm -I${REACTOS_BINARY_DIR}/sdk/include/asm <INCLUDES> <FLAGS> <DEFINES> -D__ASM__ -c <SOURCE>")

set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff <INCLUDES> <FLAGS> -DRC_INVOKED -D__WIN32__=1 -D__FLAT__=1 ${I18N_DEFS} <DEFINES> <SOURCE> <OBJECT>")

if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(RC_PREPROCESSOR_TARGET "--preprocessor-arg=--target=${CMAKE_C_COMPILER_TARGET}")
else()
    set(RC_PREPROCESSOR_TARGET "")
endif()

# When building i386 via an x86_64 multilib toolchain, ensure the RC preprocessor
# runs in 32-bit mode so macros like _WIN64 are not defined.
if(ARCH STREQUAL "i386" AND REACTOS_MULTILIB_I386)
    set(RC_PREPROCESSOR_TARGET "${RC_PREPROCESSOR_TARGET} --preprocessor-arg=-m32")
endif()

# We have to pass args to windres. one... by... one...
set(CMAKE_DEPFILE_FLAGS_RC "--preprocessor=\"${CMAKE_C_COMPILER}\" ${RC_PREPROCESSOR_TARGET} --preprocessor-arg=-E --preprocessor-arg=-nostdinc --preprocessor-arg=-xc-header --preprocessor-arg=-MMD --preprocessor-arg=-MF --preprocessor-arg=<DEPFILE> --preprocessor-arg=-MT --preprocessor-arg=<OBJECT>")

# Optional 3rd parameter: stdcall stack bytes
function(set_entrypoint MODULE ENTRYPOINT)
    # Allow per-target arch override via REACTOS_TARGET_ARCH
    get_target_property(_t_arch ${MODULE} REACTOS_TARGET_ARCH)
    if(NOT _t_arch)
        set(_t_arch ${ARCH})
    endif()
    if(${ENTRYPOINT} STREQUAL "0")
        if(_t_arch STREQUAL "arm64")
            target_link_options(${MODULE} PRIVATE "-Wl,--entry=__ReactOSNoEntry")
            target_sources(${MODULE} PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/crt/startup/noentry_arm64.c)
        endif()
    elseif(_t_arch STREQUAL "i386")
        set(_entrysymbol _${ENTRYPOINT})
        if(${ARGC} GREATER 2)
            set(_entrysymbol ${_entrysymbol}@${ARGV2})
        endif()
        target_link_options(${MODULE} PRIVATE "-Wl,--entry=${_entrysymbol}")
    else()
        target_link_options(${MODULE} PRIVATE "-Wl,--entry=${ENTRYPOINT}")
    endif()
endfunction()

function(set_subsystem MODULE SUBSYSTEM)
    if(SUBSYSTEM STREQUAL "EFI_APPLICATION" OR SUBSYSTEM STREQUAL "efi_application" OR SUBSYSTEM STREQUAL "10")
        # GNU ld accepts numeric EFI values, lld requires the named form.
        if(MINGW_LINKER_IS_LLD)
            target_link_options(${MODULE} PRIVATE "-Wl,--subsystem,efi_application")
        else()
            target_link_options(${MODULE} PRIVATE "-Wl,--subsystem,10")
        endif()
    else()
        target_link_options(${MODULE} PRIVATE "-Wl,--subsystem,${SUBSYSTEM}:5.01")
    endif()
endfunction()

function(set_image_base MODULE IMAGE_BASE)
    target_link_options(${MODULE} PRIVATE "-Wl,--image-base,${IMAGE_BASE}")
endfunction()

function(set_module_type_toolchain MODULE TYPE)
    # Allow per-target arch override via REACTOS_TARGET_ARCH
    get_target_property(_t_arch ${MODULE} REACTOS_TARGET_ARCH)
    if(NOT _t_arch)
        set(_t_arch ${ARCH})
    endif()

    target_link_options(${MODULE} PRIVATE
        -Wl,--major-image-version,5 -Wl,--minor-image-version,01 -Wl,--major-os-version,5 -Wl,--minor-os-version,01)

    if(TYPE IN_LIST KERNEL_MODULE_TYPES)
        target_link_options(${MODULE} PRIVATE -Wl,--exclude-all-symbols,-file-alignment=0x1000,-section-alignment=0x1000)

        if(${TYPE} STREQUAL "wdmdriver")
            if(NOT MINGW_LINKER_IS_LLD)
                target_link_options(${MODULE} PRIVATE "-Wl,--wdmdriver")
            endif()
        endif()

        # Place INIT &.rsrc section at the tail of the module, before .reloc
        add_linker_script(${MODULE} ${REACTOS_SOURCE_DIR}/sdk/cmake/init-section.lds)

        # Fixup section characteristics
        #  - Remove flags that LD overzealously puts (alignment flag, Initialized flags for code sections)
        #  - INIT section is made discardable
        #  - .rsrc is made read-only and discardable
        #  - PAGE & .edata sections are made pageable.
        add_custom_command(TARGET ${MODULE} POST_BUILD
            COMMAND native-pefixup --${TYPE} $<TARGET_FILE:${MODULE}>)

        # Believe it or not, cmake doesn't do that
        set_property(TARGET ${MODULE} APPEND PROPERTY LINK_DEPENDS $<TARGET_PROPERTY:native-pefixup,IMPORTED_LOCATION>)
    endif()

    # Hosted i386 modules still need libgcc helper thunks even under Clang,
    # but boot/kernellike binaries stay freestanding and opt out.
    if(_t_arch STREQUAL "i386" AND (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang"))
        set(_gcc_compat_host_types win32cui win32gui win32dll win32ocx cpl nativedll kernel kerneldll kernelmodedriver wdmdriver)
        if(TYPE IN_LIST _gcc_compat_host_types)
            target_link_options(${MODULE} PRIVATE "-Wl,--whole-archive" "$<TARGET_FILE:gcc-compat>" "-Wl,--no-whole-archive")
            add_dependencies(${MODULE} gcc-compat)
        endif()
    endif()

    if(_t_arch STREQUAL "arm64" AND (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang"))
        if(TYPE IN_LIST KERNEL_MODULE_TYPES)
            target_link_libraries(${MODULE} chkstk)
            add_dependencies(${MODULE} chkstk)
        endif()
    endif()

    # FIXME: For amd64, we need to provide CRT startup symbols
    # This is a temporary workaround - the proper fix would be to ensure
    # msvcrt.dll exports all needed startup symbols for amd64
endfunction()

function(add_delay_importlibs _module)
    get_target_property(_module_type ${_module} TYPE)
    if(_module_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "Cannot add delay imports to a static library")
    endif()
    if(ARCH STREQUAL "arm64")
        foreach(_lib ${ARGN})
            get_filename_component(_basename "${_lib}" NAME_WE)
            target_link_libraries(${_module} lib${_basename})
        endforeach()
    else()
        foreach(_lib ${ARGN})
            get_filename_component(_basename "${_lib}" NAME_WE)
            target_link_libraries(${_module} lib${_basename}_delayed)
        endforeach()
        target_link_libraries(${_module} delayimp)
    endif()
endfunction()

if(NOT ARCH STREQUAL "i386")
    set(DECO_OPTION "-@")
endif()

# Ensure dlltool gets the correct machine flags
if(ARCH STREQUAL "i386")
    # Ensure dlltool generates 32-bit code and assembles with 32-bit mode
    # Derive the 'as' program from the toolchain path (replace ...-gcc with ...-as)
    set(_dlltool_as ${CMAKE_ASM_COMPILER})
    get_filename_component(_dlltool_as_name "${_dlltool_as}" NAME)
    if(_dlltool_as_name MATCHES "gcc(\\.exe)?$")
        string(REPLACE "gcc" "as" _dlltool_as "${_dlltool_as}")
    endif()
    # x86_64-w64-mingw32-as accepts --32 to assemble 32-bit objects
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(DLLTOOL_EXTRA_ARGS -m i386 --as ${_dlltool_as} --as-flags "-m32 -c")
    else()
        set(DLLTOOL_EXTRA_ARGS -m i386 --as ${_dlltool_as} --as-flags=--32)
    endif()
elseif(ARCH STREQUAL "amd64")
    set(DLLTOOL_EXTRA_ARGS -m i386:x86-64)
elseif(ARCH STREQUAL "arm")
    set(DLLTOOL_EXTRA_ARGS -m arm)
elseif(ARCH STREQUAL "arm64")
    set(DLLTOOL_EXTRA_ARGS -m arm64)
elseif(ARCH STREQUAL "ia64")
    set(DLLTOOL_EXTRA_ARGS -m ia64)
else()
    set(DLLTOOL_EXTRA_ARGS)
endif()
if(NOT DLLTOOL_EXTRA_ARGS)
    set(DLLTOOL_EXTRA_ARGS -m i386:x86-64)
endif()

function(fixup_load_config _target)
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND native-pefixup --loadconfig "$<TARGET_FILE:${_target}>"
        COMMENT "Patching in LOAD_CONFIG"
        DEPENDS native-pefixup)
endfunction()

function(generate_import_lib _libname _dllname _spec_file __version_arg __dbg_arg)
    # Generate the def for the import lib
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.def
        COMMAND native-spec2def ${__version_arg} ${__dbg_arg} -n=${_dllname} -a=${ARCH2} ${ARGN} --implib -d=${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.def ${CMAKE_CURRENT_SOURCE_DIR}/${_spec_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_spec_file} native-spec2def)

    # For msvcrt on i386 multilib, inject aliases for underscore/dunder names used by Wine code
    # e.g. __assert -> _assert
    set(_implib_def ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.def)
    if(ARCH STREQUAL "i386" AND REACTOS_MULTILIB_I386 AND ${_dllname} STREQUAL "msvcrt.dll")
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.patched.def
            COMMAND ${CMAKE_COMMAND} -E copy ${_implib_def} ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.patched.def
            COMMAND ${CMAKE_COMMAND} -E echo " __assert=_assert" >> ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.patched.def
            COMMAND ${CMAKE_COMMAND} -E echo " stricmp=_stricmp" >> ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.patched.def
            COMMAND ${CMAKE_COMMAND} -E echo " strcmpi=_strcmpi" >> ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.patched.def
            COMMAND ${CMAKE_COMMAND} -E echo " strnicmp=_strnicmp" >> ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.patched.def
            DEPENDS ${_implib_def}
        )
        set(_implib_def ${CMAKE_CURRENT_BINARY_DIR}/${_libname}_implib.patched.def)
    endif()

    # With this, we let DLLTOOL create an import library
    set(LIBRARY_PRIVATE_DIR ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${_libname}.dir)
    # For amd64, we need to run ranlib after dlltool to add proper index
    # FIXME: For amd64, we need to run ranlib after dlltool to ensure proper index
    if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    # Prepare dlltool args per-library
    set(_dlltool_args ${DLLTOOL_EXTRA_ARGS})
        if(ARCH STREQUAL "amd64" AND NOT _dlltool_args)
            set(_dlltool_args -m i386:x86-64)
        endif()
        # Using --kill-at on i386 can drop stdcall decoration (@N) from
        # import symbols. With --disable-stdcall-fixup enabled for linkers
        # in our multilib subbuild, that causes unresolved references.
        # Avoid --kill-at for i386 multilib builds to preserve decoration.
        set(_dlltool_killat_flag -k)
        if(ARCH STREQUAL "i386" AND REACTOS_MULTILIB_I386)
            set(_dlltool_killat_flag)
        endif()
        add_custom_command(
            OUTPUT ${LIBRARY_PRIVATE_DIR}/${_libname}.a
            # Delete any existing file in the private directory before creating new one
            COMMAND ${CMAKE_COMMAND} -E rm -f ${LIBRARY_PRIVATE_DIR}/${_libname}.a
            COMMAND ${CMAKE_DLLTOOL} ${_dlltool_args} -d ${_implib_def} ${_dlltool_killat_flag} -l ${_libname}.a -t ${_libname}
            COMMAND ${CMAKE_RANLIB} ${_libname}.a
            DEPENDS ${_implib_def}
            WORKING_DIRECTORY ${LIBRARY_PRIVATE_DIR})
    else()
    # Prepare dlltool args per-library
    set(_dlltool_args ${DLLTOOL_EXTRA_ARGS})
        if(ARCH STREQUAL "amd64" AND NOT _dlltool_args)
            set(_dlltool_args -m i386:x86-64)
        endif()
        add_custom_command(
            OUTPUT ${LIBRARY_PRIVATE_DIR}/${_libname}.a
            # Delete any existing file in the private directory before creating new one
            COMMAND ${CMAKE_COMMAND} -E rm -f ${LIBRARY_PRIVATE_DIR}/${_libname}.a
            COMMAND ${CMAKE_DLLTOOL} ${_dlltool_args} -d ${_implib_def} ${_dlltool_killat_flag} -l ${_libname}.a -t ${_libname}
            DEPENDS ${_implib_def}
            WORKING_DIRECTORY ${LIBRARY_PRIVATE_DIR})
    endif()

    # We create a static library with the importlib thus created as object. AR will extract the obj files and archive it again
    set_source_files_properties(
        ${LIBRARY_PRIVATE_DIR}/${_libname}.a
        PROPERTIES
        EXTERNAL_OBJECT TRUE)
    _add_library(${_libname} STATIC EXCLUDE_FROM_ALL
        ${LIBRARY_PRIVATE_DIR}/${_libname}.a)
    set_target_properties(${_libname}
        PROPERTIES
        LINKER_LANGUAGE "C"
        PREFIX "")
    
    get_property(_has_skip_list GLOBAL PROPERTY REACTOS_SKIP_IMPORTLIB_COPY_TARGETS SET)
    if(_has_skip_list)
        get_property(_skip_import_list GLOBAL PROPERTY REACTOS_SKIP_IMPORTLIB_COPY_TARGETS)
    else()
        set(_skip_import_list)
    endif()
    list(FIND _skip_import_list ${_libname} _skip_copy_index)
    if(_skip_copy_index EQUAL -1)
        # FIXME: AR corrupts import libraries when using EXTERNAL_OBJECT
        # Force a copy operation to preserve the correct import library
        if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386" OR ARCH STREQUAL "arm" OR ARCH STREQUAL "arm64")
            # Override the library file with a proper copy after it's created
            add_custom_command(TARGET ${_libname} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy ${LIBRARY_PRIVATE_DIR}/${_libname}.a $<TARGET_FILE:${_libname}>
                COMMAND ${CMAKE_AR} s $<TARGET_FILE:${_libname}>
                COMMAND ${CMAKE_RANLIB} $<TARGET_FILE:${_libname}>
                COMMENT "FIXME: Overwriting ${_libname} with proper import library copy")
        endif()
    endif()

    if(NOT ARCH STREQUAL "arm64")
        # Do the same with delay-import libs
        set(LIBRARY_PRIVATE_DIR ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${_libname}_delayed.dir)
        # FIXME: For amd64, we need to run ranlib after dlltool to ensure proper index
        if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    # Prepare dlltool args per-library
    set(_dlltool_args ${DLLTOOL_EXTRA_ARGS})
        if(ARCH STREQUAL "amd64" AND NOT _dlltool_args)
            set(_dlltool_args -m i386:x86-64)
        endif()
        add_custom_command(
            OUTPUT ${LIBRARY_PRIVATE_DIR}/${_libname}_delayed.a
            # Delete any existing file in the private directory before creating new one
            COMMAND ${CMAKE_COMMAND} -E rm -f ${LIBRARY_PRIVATE_DIR}/${_libname}_delayed.a
            COMMAND ${CMAKE_DLLTOOL} ${_dlltool_args} --def ${_implib_def} ${_dlltool_killat_flag} --output-delaylib=${_libname}_delayed.a -t ${_libname}_delayed
            COMMAND ${CMAKE_RANLIB} ${_libname}_delayed.a
            DEPENDS ${_implib_def}
            WORKING_DIRECTORY ${LIBRARY_PRIVATE_DIR})
    else()
    # Prepare dlltool args per-library
    set(_dlltool_args ${DLLTOOL_EXTRA_ARGS})
        if(ARCH STREQUAL "amd64" AND NOT _dlltool_args)
            set(_dlltool_args -m i386:x86-64)
        endif()
        add_custom_command(
            OUTPUT ${LIBRARY_PRIVATE_DIR}/${_libname}_delayed.a
            # Delete any existing file in the private directory before creating new one
            COMMAND ${CMAKE_COMMAND} -E rm -f ${LIBRARY_PRIVATE_DIR}/${_libname}_delayed.a
            COMMAND ${CMAKE_DLLTOOL} ${_dlltool_args} --def ${_implib_def} ${_dlltool_killat_flag} --output-delaylib=${_libname}_delayed.a -t ${_libname}_delayed
            DEPENDS ${_implib_def}
            WORKING_DIRECTORY ${LIBRARY_PRIVATE_DIR})
    endif()

    # We create a static library with the importlib thus created. AR will extract the obj files and archive it again
    set_source_files_properties(
        ${LIBRARY_PRIVATE_DIR}/${_libname}_delayed.a
        PROPERTIES
        EXTERNAL_OBJECT TRUE)
    _add_library(${_libname}_delayed STATIC EXCLUDE_FROM_ALL
        ${LIBRARY_PRIVATE_DIR}/${_libname}_delayed.a)
    set_target_properties(${_libname}_delayed
        PROPERTIES
        LINKER_LANGUAGE "C"
        PREFIX "")
    
    list(FIND _skip_import_list ${_libname}_delayed _skip_delayed_copy_index)
    if(_skip_delayed_copy_index EQUAL -1)
        # FIXME: AR corrupts import libraries when using EXTERNAL_OBJECT
        # Force a copy operation to preserve the correct import library
        if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386" OR ARCH STREQUAL "arm" OR ARCH STREQUAL "arm64")
            # Override the library file with a proper copy after it's created
            add_custom_command(TARGET ${_libname}_delayed POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy ${LIBRARY_PRIVATE_DIR}/${_libname}_delayed.a $<TARGET_FILE:${_libname}_delayed>
                COMMAND ${CMAKE_AR} s $<TARGET_FILE:${_libname}_delayed>
                COMMAND ${CMAKE_RANLIB} $<TARGET_FILE:${_libname}_delayed>
                COMMENT "FIXME: Overwriting ${_libname}_delayed with proper import library copy")
        endif()
    endif()
    endif()
endfunction()

function(spec2def _dllname _spec_file)

    cmake_parse_arguments(__spec2def "ADD_IMPORTLIB;NO_PRIVATE_WARNINGS;WITH_RELAY;WITH_DBG;NO_DBG" "VERSION" "" ${ARGN})

    # Get library basename
    get_filename_component(_file ${_dllname} NAME_WE)

    # Error out on anything else than spec
    if(NOT ${_spec_file} MATCHES ".*\\.spec")
        message(FATAL_ERROR "spec2def only takes spec files as input.")
    endif()

    if(__spec2def_WITH_RELAY)
        set(__with_relay_arg "--with-tracing")
    endif()

    if(__spec2def_VERSION)
        set(__version_arg "--version=0x${__spec2def_VERSION}")
    else()
        set(__version_arg "--version=${DLL_EXPORT_VERSION}")
    endif()

    if(__spec2def_WITH_DBG OR (DBG AND NOT __spec2def_NO_DBG))
        set(__dbg_arg "--dbg")
    endif()

    # Generate exports def and C stubs file for the DLL
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_file}.def ${CMAKE_CURRENT_BINARY_DIR}/${_file}_stubs.c
        COMMAND native-spec2def -n=${_dllname} -a=${ARCH2} -d=${CMAKE_CURRENT_BINARY_DIR}/${_file}.def -s=${CMAKE_CURRENT_BINARY_DIR}/${_file}_stubs.c ${__with_relay_arg} ${__version_arg} ${__dbg_arg} ${CMAKE_CURRENT_SOURCE_DIR}/${_spec_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_spec_file} native-spec2def)

    # Do not use precompiled headers for the stub file
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${_file}_stubs.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

    if(__spec2def_ADD_IMPORTLIB)
        set(_extraflags)
        if(__spec2def_NO_PRIVATE_WARNINGS)
            set(_extraflags --no-private-warnings)
        endif()

        generate_import_lib(lib${_file} ${_dllname} ${_spec_file} ${_extraflags} "${__version_arg}" "${__dbg_arg}")
    endif()
endfunction()

macro(macro_mc FLAG FILE)
    set(_mc_target_flag "")
    if(NOT CMAKE_HOST_WIN32)
        if(ARCH STREQUAL "i386")
            set(_mc_target_flag -F pe-i386)
        elseif(ARCH STREQUAL "amd64")
            set(_mc_target_flag -F pe-x86-64)
        elseif(ARCH STREQUAL "arm")
            set(_mc_target_flag -F pe-arm-little)
        elseif(ARCH STREQUAL "arm64")
            set(_mc_target_flag -F pe-aarch64-little)
        elseif(ARCH STREQUAL "ia64")
            set(_mc_target_flag -F pe-ia64)
        endif()
    endif()
    set(COMMAND_MC ${CMAKE_MC_COMPILER} ${_mc_target_flag} -u ${FLAG} -b -h ${CMAKE_CURRENT_BINARY_DIR}/ -r ${CMAKE_CURRENT_BINARY_DIR}/ ${FILE})
endmacro()

# PSEH lib, needed with mingw
set(PSEH_LIB "pseh")

function(CreateBootSectorTarget _target_name _asm_file _binary_file _base_address)
    set(_object_file ${_binary_file}.o)

    get_defines(_defines)
    get_includes(_includes)

    add_custom_command(
        OUTPUT ${_object_file}
        COMMAND ${CMAKE_ASM_COMPILER} -x assembler-with-cpp -o ${_object_file} -I${REACTOS_SOURCE_DIR}/sdk/include/asm -I${REACTOS_BINARY_DIR}/sdk/include/asm ${_includes} ${_defines} -D__ASM__ -c ${_asm_file}
        DEPENDS ${_asm_file})

    add_custom_command(
        OUTPUT ${_binary_file}
        COMMAND native-obj2bin ${_object_file} ${_binary_file} ${_base_address}
        # COMMAND objcopy --output-target binary --image-base 0x${_base_address} ${_object_file} ${_binary_file}
        DEPENDS ${_object_file} native-obj2bin)

    set_source_files_properties(${_object_file} ${_binary_file} PROPERTIES GENERATED TRUE)

    add_custom_target(${_target_name} ALL DEPENDS ${_binary_file})
endfunction()

function(allow_warnings __module)
    # We don't allow warnings in trunk, this needs to be reworked. See CORE-6959.
    #target_compile_options(${__module} PRIVATE "-Wno-error")
endfunction()

function(convert_asm_file _source_file _target_file)
    get_filename_component(_source_file_base_name ${_source_file} NAME_WE)
    get_filename_component(_source_file_full_path ${_source_file} ABSOLUTE)
    set(_preprocessed_asm_file ${CMAKE_CURRENT_BINARY_DIR}/${_target_file})

    set(_asmpp_deps)
    if(TARGET native-asmpp)
        list(APPEND _asmpp_deps native-asmpp)
        get_target_property(_asmpp_sources native-asmpp ROS_HOST_TOOL_SOURCES)
        if(_asmpp_sources AND NOT _asmpp_sources STREQUAL "ROS_HOST_TOOL_SOURCES-NOTFOUND")
            list(APPEND _asmpp_deps ${_asmpp_sources})
        endif()
    else()
        list(APPEND _asmpp_deps native-asmpp)
    endif()
    list(REMOVE_DUPLICATES _asmpp_deps)

    add_custom_command(
        OUTPUT ${_preprocessed_asm_file}
        COMMAND native-asmpp ${_source_file_full_path} > ${_preprocessed_asm_file}
        DEPENDS ${_asmpp_deps} ${_source_file_full_path})

endfunction()

function(convert_asm_files)
    foreach(_source_file ${ARGN})
        convert_asm_file(${_source_file} ${_source_file}.s)
    endforeach()
endfunction()

macro(add_asm_files _target)
    foreach(_source_file ${ARGN})
        get_filename_component(_extension ${_source_file} EXT)
        get_filename_component(_source_file_base_name ${_source_file} NAME_WE)
        if (${_extension} STREQUAL ".asm")
            convert_asm_file(${_source_file} ${_source_file}.s)
            list(APPEND ${_target} ${CMAKE_CURRENT_BINARY_DIR}/${_source_file}.s)
        elseif (${_extension} STREQUAL ".inc")
            convert_asm_file(${_source_file} ${_source_file}.h)
            list(APPEND ${_target} ${CMAKE_CURRENT_BINARY_DIR}/${_source_file}.h)
        else()
            list(APPEND ${_target} ${_source_file})
        endif()
    endforeach()
endmacro()

function(add_linker_script _target _linker_script_file)
    if(MINGW_LINKER_IS_LLD)
        # lld COFF does not support GNU linker scripts.
        return()
    endif()
    get_filename_component(_file_full_path ${_linker_script_file} ABSOLUTE)
    target_link_options(${_target} PRIVATE "-Wl,-T,${_file_full_path}")
    set_property(TARGET ${_target} APPEND PROPERTY LINK_DEPENDS ${_file_full_path})
endfunction()

# Manage our C++ options
# we disable standard includes if we don't use the STL
add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<IN_LIST:cppstl,$<TARGET_PROPERTY:LINK_LIBRARIES>>>>:-nostdinc>")
# we disable RTTI, unless said so
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:$<TARGET_PROPERTY:WITH_CXX_RTTI>>,-frtti,-fno-rtti>>")
# We disable exceptions, unless said so
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:$<TARGET_PROPERTY:WITH_CXX_EXCEPTIONS>>,-fexceptions,-fno-exceptions>>")

# For Clang targeting 32-bit Windows we rely on its default DWARF exception
# handling instead of forcing SJLJ, which avoids missing sjlj runtime symbols.

# Find default G++ libraries
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(GXX_EXECUTABLE ${MINGW_TOOLCHAIN_PREFIX}g++${MINGW_TOOLCHAIN_SUFFIX})
    if("${GXX_EXECUTABLE}" STREQUAL "g++" AND DEFINED CMAKE_CXX_COMPILER_TARGET AND NOT "${CMAKE_CXX_COMPILER_TARGET}" STREQUAL "")
        set(GXX_EXECUTABLE ${CMAKE_CXX_COMPILER_TARGET}-g++)
    endif()
    set(_CLANG_CXX_FALLBACK "${CMAKE_CXX_COMPILER}")
    if(DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT ROS_GNU_MINGW_TOOLCHAIN_PATH STREQUAL "")
        set(_ros_gxx_candidate "${ROS_GNU_MINGW_TOOLCHAIN_PATH}/${GXX_EXECUTABLE}")
        if(EXISTS "${_ros_gxx_candidate}")
            set(GXX_EXECUTABLE "${_ros_gxx_candidate}")
        endif()
    elseif(DEFINED ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH} AND NOT "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        set(_ros_gxx_candidate "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}/${GXX_EXECUTABLE}")
        if(EXISTS "${_ros_gxx_candidate}")
            set(GXX_EXECUTABLE "${_ros_gxx_candidate}")
        endif()
    endif()
    unset(_ros_gxx_candidate)
else()
    set(GXX_EXECUTABLE ${CMAKE_CXX_COMPILER})
endif()

if(NOT IS_ABSOLUTE "${GXX_EXECUTABLE}")
    set(_gxx_search_paths)
    if(DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT ROS_GNU_MINGW_TOOLCHAIN_PATH STREQUAL "")
        list(APPEND _gxx_search_paths ${ROS_GNU_MINGW_TOOLCHAIN_PATH})
    endif()
    if(DEFINED ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH} AND NOT "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
        list(APPEND _gxx_search_paths "$ENV{ROS_GNU_MINGW_TOOLCHAIN_PATH}")
    endif()
    if(DEFINED TOOLCHAIN_PATH AND NOT TOOLCHAIN_PATH STREQUAL "")
        list(APPEND _gxx_search_paths ${TOOLCHAIN_PATH})
    endif()
    find_program(GXX_EXECUTABLE_FULL
        NAMES ${GXX_EXECUTABLE}
        HINTS ${_gxx_search_paths}
        NO_DEFAULT_PATH)
    if(NOT GXX_EXECUTABLE_FULL)
        find_program(GXX_EXECUTABLE_FULL NAMES ${GXX_EXECUTABLE})
    endif()
    if(GXX_EXECUTABLE_FULL)
        set(GXX_EXECUTABLE ${GXX_EXECUTABLE_FULL})
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        if(_CLANG_CXX_FALLBACK)
            set(GXX_EXECUTABLE ${_CLANG_CXX_FALLBACK})
        else()
            message(FATAL_ERROR
                "${GXX_EXECUTABLE} not found. Ensure the MinGW toolchain is installed "
                "and either add it to PATH or set TOOLCHAIN_PATH.")
        endif()
    endif()
    unset(_gxx_search_paths)
endif()
unset(_CLANG_CXX_FALLBACK)

# Allow MMX/SSE2 builtins when using clang (llvm-mingw); some headers emit MMX intrinsics.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmmx -msse2" CACHE STRING "C compiler flags" FORCE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmmx -msse2" CACHE STRING "C++ compiler flags" FORCE)
    endif()
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
        OUTPUT_VARIABLE CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(CLANG_RESOURCE_DIR)
        set(_clang_resource_include "${CLANG_RESOURCE_DIR}/include")
        if(EXISTS "${_clang_resource_include}")
            string(FIND "${CMAKE_C_FLAGS}" "-isystem${_clang_resource_include}" _clang_resource_index)
            if(_clang_resource_index EQUAL -1)
                set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isystem${_clang_resource_include}" CACHE STRING "C compiler flags" FORCE)
            endif()
            string(FIND "${CMAKE_CXX_FLAGS}" "-isystem${_clang_resource_include}" _clang_resource_cxx_index)
            if(_clang_resource_cxx_index EQUAL -1)
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem${_clang_resource_include}" CACHE STRING "C++ compiler flags" FORCE)
            endif()
        endif()
    endif()
endif()

set(GXX_MULTIARCH_ARGS)
if(ARCH STREQUAL "i386")
    set(GXX_MULTIARCH_ARGS -m32)
endif()

execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libwinpthread.a OUTPUT_VARIABLE LIBWINPTHREAD_LOCATION)
if(LIBWINPTHREAD_LOCATION MATCHES "mingw32")
    add_library(libwinpthread STATIC IMPORTED)
    string(STRIP "${LIBWINPTHREAD_LOCATION}" LIBWINPTHREAD_LOCATION)
    message(STATUS "Using libwinpthread from ${LIBWINPTHREAD_LOCATION}")
    set_target_properties(libwinpthread PROPERTIES IMPORTED_LOCATION ${LIBWINPTHREAD_LOCATION})
    if(ARCH STREQUAL "arm64")
        # Arm64 freeldr links a static CRT; avoid dragging msvcrt in here. FIXME: revisit once we have a dedicated winpthread import lib.
        target_link_libraries(libwinpthread INTERFACE libkernel32 libntdll)
    else()
        # libwinpthread needs kernel32 imports, a CRT and msvcrtex
        target_link_libraries(libwinpthread INTERFACE libkernel32 libmsvcrt msvcrtex)
    endif()
else()
    add_library(libwinpthread INTERFACE)
endif()

execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libatomic.a OUTPUT_VARIABLE LIBATOMIC_LOCATION)
string(STRIP "${LIBATOMIC_LOCATION}" LIBATOMIC_LOCATION)
get_filename_component(_LIBATOMIC_REALPATH "${LIBATOMIC_LOCATION}" REALPATH)
if(EXISTS "${_LIBATOMIC_REALPATH}")
    add_library(libatomic STATIC IMPORTED)
    set_target_properties(libatomic PROPERTIES IMPORTED_LOCATION ${LIBATOMIC_LOCATION})
    # libatomic may use the same TLS helpers as libgcc on MinGW targets
    target_link_libraries(libatomic INTERFACE libkernel32 libntdll)
else()
    message(STATUS "libatomic.a not found; skipping libatomic")
endif()
unset(_LIBATOMIC_REALPATH)

add_library(libgcc STATIC IMPORTED)
execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libgcc.a OUTPUT_VARIABLE LIBGCC_LOCATION)
string(STRIP "${LIBGCC_LOCATION}" LIBGCC_LOCATION)
get_filename_component(_LIBGCC_REALPATH "${LIBGCC_LOCATION}" REALPATH)
if(NOT EXISTS "${_LIBGCC_REALPATH}")
    get_filename_component(_LIBGCC_BIN_DIR "${GXX_EXECUTABLE}" DIRECTORY)
    get_filename_component(_LIBGCC_TOOLCHAIN_ROOT "${_LIBGCC_BIN_DIR}" DIRECTORY)
    set(_LIBGCC_BUILTINS_ARCH x86_64)
    if(ARCH STREQUAL "i386")
        set(_LIBGCC_BUILTINS_ARCH i386)
    elseif(ARCH STREQUAL "arm")
        set(_LIBGCC_BUILTINS_ARCH arm)
    elseif(ARCH STREQUAL "arm64")
        set(_LIBGCC_BUILTINS_ARCH aarch64)
    endif()
    file(GLOB _LIBGCC_BUILTINS_CANDIDATES "${_LIBGCC_TOOLCHAIN_ROOT}/lib/clang/*/lib/windows/libclang_rt.builtins-${_LIBGCC_BUILTINS_ARCH}.a")
    list(LENGTH _LIBGCC_BUILTINS_CANDIDATES _LIBGCC_BUILTINS_COUNT)
    if(_LIBGCC_BUILTINS_COUNT GREATER 0)
        list(GET _LIBGCC_BUILTINS_CANDIDATES 0 LIBGCC_LOCATION)
    else()
        message(WARNING "libgcc.a not found; compiler-rt builtins will not be linked automatically")
    endif()
    unset(_LIBGCC_BUILTINS_CANDIDATES)
    unset(_LIBGCC_BUILTINS_COUNT)
    unset(_LIBGCC_BUILTINS_ARCH)
    unset(_LIBGCC_BIN_DIR)
    unset(_LIBGCC_TOOLCHAIN_ROOT)
endif()
set_target_properties(libgcc PROPERTIES IMPORTED_LOCATION ${LIBGCC_LOCATION})
# libgcc needs kernel32/ntdll for SEH helpers and winpthread for TLS init
target_link_libraries(libgcc INTERFACE libwinpthread libkernel32 libntdll)

add_library(libsupc++ STATIC IMPORTED GLOBAL)
execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libsupc++.a OUTPUT_VARIABLE LIBSUPCXX_LOCATION)
string(STRIP "${LIBSUPCXX_LOCATION}" LIBSUPCXX_LOCATION)
get_filename_component(_LIBSUPCXX_REALPATH "${LIBSUPCXX_LOCATION}" REALPATH)
if(NOT LIBSUPCXX_LOCATION OR LIBSUPCXX_LOCATION STREQUAL "libsupc++.a" OR NOT EXISTS "${_LIBSUPCXX_REALPATH}")
    get_filename_component(_LIBSUPCXX_BIN_DIR "${GXX_EXECUTABLE}" DIRECTORY)
    get_filename_component(_LIBSUPCXX_TOOLCHAIN_ROOT "${_LIBSUPCXX_BIN_DIR}" DIRECTORY)
    find_library(_LIBSUPCXX_ALT NAMES c++abi libc++abi PATHS
        "${_LIBSUPCXX_TOOLCHAIN_ROOT}/${CMAKE_CXX_COMPILER_TARGET}/lib"
        "${_LIBSUPCXX_TOOLCHAIN_ROOT}/${MINGW_TOOLCHAIN_PREFIX}/lib"
        "${_LIBSUPCXX_TOOLCHAIN_ROOT}/generic-w64-mingw32/lib"
        "${_LIBSUPCXX_TOOLCHAIN_ROOT}/x86_64-w64-mingw32/lib"
        "${_LIBSUPCXX_TOOLCHAIN_ROOT}/i686-w64-mingw32/lib"
        "${_LIBSUPCXX_TOOLCHAIN_ROOT}/lib"
        NO_DEFAULT_PATH)
    if(_LIBSUPCXX_ALT)
        set(LIBSUPCXX_LOCATION "${_LIBSUPCXX_ALT}")
        set(_LIBSTDCCXX_IS_LIBCXX TRUE)
    else()
        message(WARNING "libsupc++.a not found; using toolchain defaults may fail to link C++ exceptions")
    endif()
    unset(_LIBSUPCXX_ALT)
    unset(_LIBSUPCXX_BIN_DIR)
    unset(_LIBSUPCXX_TOOLCHAIN_ROOT)
endif()
set_target_properties(libsupc++ PROPERTIES IMPORTED_LOCATION ${LIBSUPCXX_LOCATION})

# Add libgcc_eh for exception handling on amd64
if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    add_library(libgcc_eh STATIC IMPORTED GLOBAL)
    execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libgcc_eh.a OUTPUT_VARIABLE LIBGCCEH_LOCATION)
    string(STRIP "${LIBGCCEH_LOCATION}" LIBGCCEH_LOCATION)
    get_filename_component(_LIBGCCEH_REALPATH "${LIBGCCEH_LOCATION}" REALPATH)
    if(NOT EXISTS "${_LIBGCCEH_REALPATH}")
        get_filename_component(_LIBGCCEH_BIN_DIR "${GXX_EXECUTABLE}" DIRECTORY)
        get_filename_component(_LIBGCCEH_TOOLCHAIN_ROOT "${_LIBGCCEH_BIN_DIR}" DIRECTORY)
        find_library(_LIBGCCEH_ALT NAMES unwind libunwind PATHS
            "${_LIBGCCEH_TOOLCHAIN_ROOT}/${CMAKE_CXX_COMPILER_TARGET}/lib"
            "${_LIBGCCEH_TOOLCHAIN_ROOT}/${MINGW_TOOLCHAIN_PREFIX}/lib"
            "${_LIBGCCEH_TOOLCHAIN_ROOT}/generic-w64-mingw32/lib"
            "${_LIBGCCEH_TOOLCHAIN_ROOT}/x86_64-w64-mingw32/lib"
            "${_LIBGCCEH_TOOLCHAIN_ROOT}/i686-w64-mingw32/lib"
            "${_LIBGCCEH_TOOLCHAIN_ROOT}/lib"
            NO_DEFAULT_PATH)
        if(_LIBGCCEH_ALT)
            set(LIBGCCEH_LOCATION "${_LIBGCCEH_ALT}")
        else()
            message(WARNING "libgcc_eh.a not found; falling back may be required for exception handling")
        endif()
        unset(_LIBGCCEH_ALT)
        unset(_LIBGCCEH_BIN_DIR)
        unset(_LIBGCCEH_TOOLCHAIN_ROOT)
    endif()
    set_target_properties(libgcc_eh PROPERTIES IMPORTED_LOCATION ${LIBGCCEH_LOCATION})
    # libsupc++ requires libgcc_eh, libgcc and stdc++compat
    target_link_libraries(libsupc++ INTERFACE libgcc_eh libgcc stdc++compat)
else()
    # libsupc++ requires libgcc and stdc++compat
    target_link_libraries(libsupc++ INTERFACE libgcc stdc++compat)
endif()

add_library(libmingwex STATIC IMPORTED)
execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libmingwex.a OUTPUT_VARIABLE LIBMINGWEX_LOCATION)
string(STRIP "${LIBMINGWEX_LOCATION}" LIBMINGWEX_LOCATION)
set_target_properties(libmingwex PROPERTIES IMPORTED_LOCATION ${LIBMINGWEX_LOCATION})
# libmingwex requires a CRT and imports from kernel32
target_link_libraries(libmingwex INTERFACE libmsvcrt libkernel32)

add_library(libstdc++ STATIC IMPORTED GLOBAL)
execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libstdc++.a OUTPUT_VARIABLE LIBSTDCCXX_LOCATION)
string(STRIP "${LIBSTDCCXX_LOCATION}" LIBSTDCCXX_LOCATION)
set_target_properties(libstdc++ PROPERTIES IMPORTED_LOCATION ${LIBSTDCCXX_LOCATION})
# Derive the MinGW C++ include directories so Clang can find standard headers.
get_filename_component(_LIBSTDCCXX_REALPATH "${LIBSTDCCXX_LOCATION}" REALPATH)
if(NOT EXISTS "${_LIBSTDCCXX_REALPATH}")
    set(_LIBSTDCCXX_REALPATH "")
endif()
get_filename_component(_GXX_BIN_DIR "${GXX_EXECUTABLE}" DIRECTORY)
get_filename_component(_GXX_TOOLCHAIN_ROOT "${_GXX_BIN_DIR}" DIRECTORY)
set(_LIBSTDCCXX_IS_LIBCXX FALSE)
if(NOT _LIBSTDCCXX_REALPATH)
    set(_LIBCXX_STATIC_CANDIDATES "")
    if(CMAKE_CXX_COMPILER_TARGET)
        list(APPEND _LIBCXX_STATIC_CANDIDATES
            "${_GXX_TOOLCHAIN_ROOT}/${CMAKE_CXX_COMPILER_TARGET}/lib/libc++.a")
    endif()
    if(MINGW_TOOLCHAIN_PREFIX)
        list(APPEND _LIBCXX_STATIC_CANDIDATES
            "${_GXX_TOOLCHAIN_ROOT}/${MINGW_TOOLCHAIN_PREFIX}/lib/libc++.a")
    endif()
    if(_LIBSTDCCXX_TRIPLET)
        list(APPEND _LIBCXX_STATIC_CANDIDATES
            "${_GXX_TOOLCHAIN_ROOT}/${_LIBSTDCCXX_TRIPLET}/lib/libc++.a")
    endif()
    list(APPEND _LIBCXX_STATIC_CANDIDATES
        "${_GXX_TOOLCHAIN_ROOT}/aarch64-w64-mingw32/lib/libc++.a"
        "${_GXX_TOOLCHAIN_ROOT}/x86_64-w64-mingw32/lib/libc++.a"
        "${_GXX_TOOLCHAIN_ROOT}/i686-w64-mingw32/lib/libc++.a"
        "${_GXX_TOOLCHAIN_ROOT}/generic-w64-mingw32/lib/libc++.a")
    foreach(_LIBCXX_CAND IN LISTS _LIBCXX_STATIC_CANDIDATES)
        if(EXISTS "${_LIBCXX_CAND}")
            set(_LIBCXX_STATIC "${_LIBCXX_CAND}")
            break()
        endif()
    endforeach()
    if(NOT _LIBCXX_STATIC)
        find_library(_LIBCXX_STATIC NAMES libc++.a PATHS
            "${_GXX_TOOLCHAIN_ROOT}/${CMAKE_CXX_COMPILER_TARGET}/lib"
            "${_GXX_TOOLCHAIN_ROOT}/${CMAKE_CXX_COMPILER_TARGET}/lib64"
            "${_GXX_TOOLCHAIN_ROOT}/${MINGW_TOOLCHAIN_PREFIX}/lib"
            "${_GXX_TOOLCHAIN_ROOT}/generic-w64-mingw32/lib"
            "${_GXX_TOOLCHAIN_ROOT}/x86_64-w64-mingw32/lib"
            "${_GXX_TOOLCHAIN_ROOT}/i686-w64-mingw32/lib"
            "${_GXX_TOOLCHAIN_ROOT}/lib"
            "${_GXX_TOOLCHAIN_ROOT}/lib64"
            NO_DEFAULT_PATH)
    endif()
    if(_LIBCXX_STATIC)
        set(LIBSTDCCXX_LOCATION "${_LIBCXX_STATIC}")
        set(_LIBSTDCCXX_REALPATH "${_LIBCXX_STATIC}")
        set(_LIBSTDCCXX_IS_LIBCXX TRUE)
    endif()
endif()
get_filename_component(_LIBSTDCCXX_DIR "${_LIBSTDCCXX_REALPATH}" DIRECTORY)
get_filename_component(_LIBSTDCCXX_VERSION "${_LIBSTDCCXX_DIR}" NAME)
get_filename_component(_LIBSTDCCXX_TRIPLET_DIR "${_LIBSTDCCXX_DIR}" DIRECTORY)
get_filename_component(_LIBSTDCCXX_TRIPLET "${_LIBSTDCCXX_TRIPLET_DIR}" NAME)
get_filename_component(_LIBSTDCCXX_LIBGCC_DIR "${_LIBSTDCCXX_TRIPLET_DIR}" DIRECTORY)
get_filename_component(_LIBSTDCCXX_LIB_DIR "${_LIBSTDCCXX_LIBGCC_DIR}" DIRECTORY)
get_filename_component(_LIBSTDCCXX_PREFIX "${_LIBSTDCCXX_LIB_DIR}" DIRECTORY)
set(_LIBSTDCCXX_FALLBACK_INCLUDE_BASE "${_LIBSTDCCXX_PREFIX}/${_LIBSTDCCXX_TRIPLET}/include/c++/${_LIBSTDCCXX_VERSION}")
set(_LIBSTDCCXX_TRIPLET_CANDIDATE "${CMAKE_CXX_COMPILER_TARGET}")
if(NOT _LIBSTDCCXX_TRIPLET_CANDIDATE)
    set(_LIBSTDCCXX_TRIPLET_CANDIDATE "${_LIBSTDCCXX_TRIPLET}")
endif()
if(NOT _LIBSTDCCXX_TRIPLET_CANDIDATE)
    execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -dumpmachine OUTPUT_VARIABLE _LIBSTDCCXX_TRIPLET_CANDIDATE ERROR_QUIET)
    string(STRIP "${_LIBSTDCCXX_TRIPLET_CANDIDATE}" _LIBSTDCCXX_TRIPLET_CANDIDATE)
endif()
if(NOT _LIBSTDCCXX_TRIPLET_CANDIDATE AND DEFINED MINGW_TOOLCHAIN_PREFIX)
    set(_LIBSTDCCXX_TRIPLET_CANDIDATE "${MINGW_TOOLCHAIN_PREFIX}")
    string(REGEX REPLACE "-$" "" _LIBSTDCCXX_TRIPLET_CANDIDATE "${_LIBSTDCCXX_TRIPLET_CANDIDATE}")
endif()
execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -dumpfullversion OUTPUT_VARIABLE _GXX_FULL_VERSION ERROR_QUIET)
string(STRIP "${_GXX_FULL_VERSION}" _GXX_FULL_VERSION)
if(NOT _GXX_FULL_VERSION)
    execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -dumpversion OUTPUT_VARIABLE _GXX_FULL_VERSION ERROR_QUIET)
    string(STRIP "${_GXX_FULL_VERSION}" _GXX_FULL_VERSION)
endif()
if(NOT _GXX_FULL_VERSION)
    set(_GXX_FULL_VERSION "${_LIBSTDCCXX_VERSION}")
endif()
if(NOT _GXX_FULL_VERSION)
    set(_GXX_FULL_VERSION "v1")
endif()
if(NOT _LIBSTDCCXX_LIB_DIR OR _LIBSTDCCXX_LIB_DIR STREQUAL ".")
    set(_LIBSTDCCXX_LIB_DIR "${_GXX_TOOLCHAIN_ROOT}/${_LIBSTDCCXX_TRIPLET_CANDIDATE}/lib")
endif()
set(_LIBSTDCCXX_INCLUDE_BASE "${_GXX_TOOLCHAIN_ROOT}/${_LIBSTDCCXX_TRIPLET_CANDIDATE}/include/c++/${_GXX_FULL_VERSION}")
if(NOT EXISTS "${_LIBSTDCCXX_INCLUDE_BASE}" AND DEFINED MINGW_TOOLCHAIN_PREFIX AND NOT "${MINGW_TOOLCHAIN_PREFIX}" STREQUAL "")
    string(REGEX REPLACE "-$" "" _CLANG_MINGW_TRIPLET "${MINGW_TOOLCHAIN_PREFIX}")
    if(_CLANG_MINGW_TRIPLET AND EXISTS "${_GXX_TOOLCHAIN_ROOT}/${_CLANG_MINGW_TRIPLET}/include/c++/${_GXX_FULL_VERSION}")
        set(_LIBSTDCCXX_TRIPLET_CANDIDATE "${_CLANG_MINGW_TRIPLET}")
        set(_LIBSTDCCXX_INCLUDE_BASE "${_GXX_TOOLCHAIN_ROOT}/${_LIBSTDCCXX_TRIPLET_CANDIDATE}/include/c++/${_GXX_FULL_VERSION}")
    endif()
    unset(_CLANG_MINGW_TRIPLET)
endif()
if(NOT EXISTS "${_LIBSTDCCXX_INCLUDE_BASE}")
    foreach(_LIBCXX_BASE IN ITEMS
        "${_GXX_TOOLCHAIN_ROOT}/${_LIBSTDCCXX_TRIPLET_CANDIDATE}/include/c++/v1"
        "${_GXX_TOOLCHAIN_ROOT}/generic-w64-mingw32/include/c++/v1"
        "${_GXX_TOOLCHAIN_ROOT}/include/c++/v1"
        "${_GXX_TOOLCHAIN_ROOT}/share/libc++/v1")
        if(EXISTS "${_LIBCXX_BASE}")
            set(_LIBSTDCCXX_INCLUDE_BASE "${_LIBCXX_BASE}")
            set(_LIBSTDCCXX_IS_LIBCXX TRUE)
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${_LIBSTDCCXX_INCLUDE_BASE}")
    set(_LIBSTDCCXX_INCLUDE_BASE "${_LIBSTDCCXX_FALLBACK_INCLUDE_BASE}")
endif()
set(_LIBSTDCCXX_INCLUDE_DIRS)
if(EXISTS "${_LIBSTDCCXX_INCLUDE_BASE}")
    list(APPEND _LIBSTDCCXX_INCLUDE_DIRS ${_LIBSTDCCXX_INCLUDE_BASE})
    if(EXISTS "${_LIBSTDCCXX_INCLUDE_BASE}/${_LIBSTDCCXX_TRIPLET_CANDIDATE}")
        list(APPEND _LIBSTDCCXX_INCLUDE_DIRS ${_LIBSTDCCXX_INCLUDE_BASE}/${_LIBSTDCCXX_TRIPLET_CANDIDATE})
    endif()
    if(EXISTS "${_LIBSTDCCXX_INCLUDE_BASE}/backward")
        list(APPEND _LIBSTDCCXX_INCLUDE_DIRS ${_LIBSTDCCXX_INCLUDE_BASE}/backward)
    endif()
else()
    message(WARNING "libstdc++ headers not found at ${_LIBSTDCCXX_INCLUDE_BASE}; C++ builds may fail.")
endif()
set(_LIBSTDCCXX_EXTRA_INCLUDE_DIRS
    "${_GXX_TOOLCHAIN_ROOT}/${_LIBSTDCCXX_TRIPLET_CANDIDATE}/sysroot/usr/${_LIBSTDCCXX_TRIPLET_CANDIDATE}/include"
    "${_GXX_TOOLCHAIN_ROOT}/${_LIBSTDCCXX_TRIPLET_CANDIDATE}/sysroot/mingw/include")
foreach(_LIBSTDCCXX_EXTRA_DIR IN LISTS _LIBSTDCCXX_EXTRA_INCLUDE_DIRS)
    if(EXISTS "${_LIBSTDCCXX_EXTRA_DIR}")
        list(APPEND _LIBSTDCCXX_INCLUDE_DIRS ${_LIBSTDCCXX_EXTRA_DIR})
    endif()
endforeach()
unset(_LIBSTDCCXX_EXTRA_INCLUDE_DIRS)
set_target_properties(libstdc++ PROPERTIES IMPORTED_LOCATION ${LIBSTDCCXX_LOCATION})
message(STATUS "Detected libstdc++ include roots: ${_LIBSTDCCXX_INCLUDE_DIRS}")
set(REACTOS_CXX_STL_INCLUDE_DIRS)
set(REACTOS_CXX_STL_FORCE_NON_SYSTEM FALSE)
if(_LIBSTDCCXX_INCLUDE_DIRS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(_STDCPP_INCLUDE_DIR IN LISTS _LIBSTDCCXX_INCLUDE_DIRS)
        list(APPEND REACTOS_CXX_STL_INCLUDE_DIRS "$<$<COMPILE_LANGUAGE:CXX>:${_STDCPP_INCLUDE_DIR}>")
    endforeach()
endif()
if(_LIBSTDCCXX_IS_LIBCXX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(_STDCPP_INCLUDE_DIR IN LISTS _LIBSTDCCXX_INCLUDE_DIRS)
        list(REMOVE_ITEM CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "${_STDCPP_INCLUDE_DIR}")
    endforeach()
    set(REACTOS_CXX_STL_FORCE_NON_SYSTEM TRUE)
endif()
if(_LIBSTDCCXX_IS_LIBCXX)
    add_compile_definitions($<$<COMPILE_LANGUAGE:CXX>:REACTOS_LIBCXX>)
endif()
unset(_GXX_BIN_DIR)
unset(_GXX_TOOLCHAIN_ROOT)
unset(_GXX_FULL_VERSION)
unset(_LIBSTDCCXX_TRIPLET_CANDIDATE)
unset(_LIBSTDCCXX_FALLBACK_INCLUDE_BASE)
# libstdc++ requires libsupc++ and mingwex provided by GCC
set(_LIBSTDCCXX_DEPS libsupc++ libmingwex oldnames)
    if(_LIBSTDCCXX_IS_LIBCXX)
        set(_LIBSTDCCXX_DEPS libmingwex oldnames stdc++compat)
        find_library(_LIBCXXABI_STATIC NAMES c++abi libc++abi PATHS
            "${_LIBSTDCCXX_LIB_DIR}"
            "${_LIBSTDCCXX_LIB_DIR}/../lib"
            "${_GXX_TOOLCHAIN_ROOT}/lib"
            NO_DEFAULT_PATH)
        execute_process(COMMAND ${GXX_EXECUTABLE} ${GXX_MULTIARCH_ARGS} -print-file-name=libunwind.a
            OUTPUT_VARIABLE _LIBUNWIND_STATIC)
        string(STRIP "${_LIBUNWIND_STATIC}" _LIBUNWIND_STATIC)
        if(NOT EXISTS "${_LIBUNWIND_STATIC}")
            set(_LIBUNWIND_STATIC "")
        endif()
        if(NOT _LIBUNWIND_STATIC)
            find_library(_LIBUNWIND_STATIC NAMES unwind libunwind PATHS
                "${_LIBSTDCCXX_LIB_DIR}"
                "${_LIBSTDCCXX_LIB_DIR}/../lib"
                "${_GXX_TOOLCHAIN_ROOT}/lib"
                NO_DEFAULT_PATH)
        endif()
    if(_LIBCXXABI_STATIC)
        list(APPEND _LIBSTDCCXX_DEPS "${_LIBCXXABI_STATIC}")
    endif()
    if(NOT _LIBUNWIND_STATIC)
        set(_LIBUNWIND_CANDIDATES
            "${_LIBSTDCCXX_LIB_DIR}/libunwind.a"
            "${_LIBSTDCCXX_LIB_DIR}/libunwind.dll.a"
            "${_LIBSTDCCXX_LIB_DIR}/../lib/libunwind.a"
            "${_LIBSTDCCXX_LIB_DIR}/../lib/libunwind.dll.a")
        foreach(_LIBUNWIND_CAND IN LISTS _LIBUNWIND_CANDIDATES)
            if(EXISTS "${_LIBUNWIND_CAND}")
                set(_LIBUNWIND_STATIC "${_LIBUNWIND_CAND}")
                break()
            endif()
        endforeach()
        unset(_LIBUNWIND_CANDIDATES)
    endif()
    if(_LIBUNWIND_STATIC)
        list(APPEND _LIBSTDCCXX_DEPS "${_LIBUNWIND_STATIC}")
    endif()
endif()
target_link_libraries(libstdc++ INTERFACE ${_LIBSTDCCXX_DEPS})
unset(_LIBCXXABI_STATIC)
unset(_LIBUNWIND_STATIC)
unset(_LIBSTDCCXX_DEPS)

# Helper interface library that carries the C++ STL usage requirements
add_library(cppstl INTERFACE)
target_link_libraries(cppstl INTERFACE libstdc++)
if(_LIBSTDCCXX_INCLUDE_DIRS)
    foreach(_STDCPP_INCLUDE_DIR IN LISTS _LIBSTDCCXX_INCLUDE_DIRS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            if(REACTOS_CXX_STL_FORCE_NON_SYSTEM)
                target_include_directories(cppstl BEFORE INTERFACE
                    "$<$<COMPILE_LANGUAGE:CXX>:${_STDCPP_INCLUDE_DIR}>")
            else()
                target_include_directories(cppstl SYSTEM BEFORE INTERFACE
                    "$<$<COMPILE_LANGUAGE:CXX>:${_STDCPP_INCLUDE_DIR}>")
            endif()
        else()
            if(REACTOS_CXX_STL_FORCE_NON_SYSTEM)
                target_include_directories(cppstl BEFORE INTERFACE "${_STDCPP_INCLUDE_DIR}")
            else()
                target_include_directories(cppstl SYSTEM BEFORE INTERFACE "${_STDCPP_INCLUDE_DIR}")
            endif()
        endif()
    endforeach()
endif()
if(_LIBSTDCCXX_IS_LIBCXX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Keep libc++ headers ahead of the CRT search paths
    target_compile_options(cppstl INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>")
endif()
target_compile_definitions(cppstl INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:PAL_STDCPP_COMPAT>")
unset(_LIBSTDCCXX_INCLUDE_DIRS)

set(CMAKE_AR_FLAGS "rcs")
