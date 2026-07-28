
if(ARCH STREQUAL "i386")
    set(SARCH "pc" CACHE STRING
    "Sub-architecture to build for. Specify one of:
     pc pc98 xbox")
elseif(ARCH STREQUAL "amd64")
    set(SARCH "" CACHE STRING
    "Sub-architecture to build for.")
elseif(ARCH STREQUAL "arm")
    set(SARCH "omap3-zoom2" CACHE STRING
    "Sub-architecture (board) to build for. Specify one of:
     kurobox versatile omap3-zoom2 omap3-beagle")
elseif(ARCH STREQUAL "arm64")
    # By design, arm64 kernels and OSes should be intercompatible, but
    # due to SoC vendors seemingly not being able to follow ARM design guidelines
    # properly, there might be a need for board-specific builds later on...
    set(SARCH "" CACHE STRING
    "Sub-architecture (board) to build for.")
endif()

if(ARCH STREQUAL "i386")
    set(OARCH "pentium" CACHE STRING
    "Generate instructions for this CPU type. Specify one of:
     pentium, pentiumpro")
elseif(ARCH STREQUAL "amd64")
    set(OARCH "athlon64" CACHE STRING
    "Generate instructions for this CPU type. Specify one of:
     k8 opteron athlon64 athlon-fx")
elseif(ARCH STREQUAL "arm")
    set(OARCH "armv7-a" CACHE STRING
    "Generate instructions for this CPU type. Specify one of:
     armv5te armv7-a")
elseif(ARCH STREQUAL "arm64")
    # This should not be bumped unless REALLY needed, because (as of 2021)
    # there are still new designs using the original A53 cores w/ armv8.0.
    set(OARCH "armv8-a" CACHE STRING
    "Generate instructions for this CPU type. Specify one of:
     armv8-a armv8.1-a armv8.2-a armv8.3-a armv8.4-a armv8.5-a armv8.6-a")
endif()

if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    set(TUNE "generic" CACHE STRING
    "Which CPU ReactOS should be optimized for.")
elseif(ARCH STREQUAL "arm")
    set(TUNE "generic-armv7-a" CACHE STRING
    "Which CPU ReactOS should be optimized for.")
elseif(ARCH STREQUAL "arm64")
    set(TUNE "generic" CACHE STRING
    "Which CPU ReactOS should be optimized for.")
endif()

set(OPTIMIZE "4" CACHE STRING
"What level of optimization to use.
 0 = Off
 1 = Optimize for size (-Os) with some additional options
 2 = Optimize for size (-Os)
 3 = Optimize debugging experience (-Og)
 4 = Optimize (-O1)
 5 = Optimize even more (-O2)
 6 = Optimize yet more (-O3)
 7 = Disregard strict standards compliance (-Ofast)")

set(LTCG FALSE CACHE BOOL
"Whether to build with link-time code generation")

set(GDB FALSE CACHE BOOL
"Whether to use by default KDGDB.DLL instead of KDCOM.DLL for debugging with GDB.
Mainly used for cloud-based ReactOS development using Gitpod and Docker.
If you don't use GDB, don't enable this.")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_REACTOS_DEFAULT_DBG TRUE)
else()
    set(_REACTOS_DEFAULT_DBG FALSE)
endif()

set(DBG ${_REACTOS_DEFAULT_DBG} CACHE BOOL
"Whether to compile for debugging.")

set(SEPARATE_DBG TRUE CACHE BOOL
"Whether to generate separate debug symbol files.")

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR
   CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR
   SEPARATE_DBG)
    set(_REACTOS_DEFAULT_DEBUG_SYMBOLS TRUE)
else()
    set(_REACTOS_DEFAULT_DEBUG_SYMBOLS FALSE)
endif()

set(WITH_DEBUG_SYMBOLS ${_REACTOS_DEFAULT_DEBUG_SYMBOLS} CACHE BOOL
"Whether to compile debug information.")

if(MSVC)
    set(KDBG FALSE CACHE BOOL
"Whether to compile in the integrated kernel debugger.")
    if(DBG)
        set(_WINKD_ TRUE CACHE BOOL "Whether to compile with the KD protocol.")
    else()
        set(_WINKD_ FALSE CACHE BOOL "Whether to compile with the KD protocol.")
    endif()
else()
    if(DBG)
        set(KDBG TRUE CACHE BOOL "Whether to compile in the integrated kernel debugger.")
    else()
        set(KDBG FALSE CACHE BOOL "Whether to compile in the integrated kernel debugger.")
    endif()
    set(_WINKD_ FALSE CACHE BOOL "Whether to compile with the KD protocol.")
endif()

if(GDB)
    if(NOT (ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64"))
        message(FATAL_ERROR "KDGDB is only supported on i386 and amd64")
    endif()
    # KDGDB speaks the KD protocol and replaces the integrated debugger.
    # These deliberately shadow the cache entries set above, so that turning
    # GDB on takes effect even in an already configured build directory.
    set(KDBG FALSE)
    set(_WINKD_ TRUE)
endif()

cmake_dependent_option(ISAPNP_ENABLE "Whether to enable the ISA PnP support." ON
                       "ARCH STREQUAL i386 AND NOT SARCH STREQUAL xbox" OFF)

set(GENERATE_DEPENDENCY_GRAPH FALSE CACHE BOOL
"Whether to create a GraphML dependency graph of DLLs.")

cmake_dependent_option(ENABLE_ROSTESTS "Whether to build the ReactOS test suite." OFF
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

cmake_dependent_option(ENABLE_FEX_ARM64EC
                       "Whether to build the optional FEX ARM64EC emulator for running AMD64 binaries on ARM64." OFF
                       "ARCH STREQUAL arm64" OFF)

cmake_dependent_option(ENABLE_WOW64 "Whether to build the amd64 WoW64 subsystem." OFF
                       "ARCH STREQUAL amd64" OFF)

cmake_dependent_option(ENABLE_ROSV
                       "Whether to build the ROSV VMX hypervisor driver and its user-mode tools." OFF
                       "ARCH STREQUAL amd64" OFF)

# Set by the nested build that wow64.cmake configures: marks this i386 tree
# as the 32-bit guest half of an amd64 WoW64 build.
cmake_dependent_option(WOW64_I386_RUNTIME
                       "Whether this i386 build provides the 32-bit guest half of WoW64." OFF
                       "ARCH STREQUAL i386" OFF)

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    option(_PREFAST_ "Whether to enable PREFAST while compiling." OFF)
    option(_VS_ANALYZE_ "Whether to enable static analysis while compiling." OFF)
    # RTC are incompatible with compiler optimizations.
    cmake_dependent_option(RUNTIME_CHECKS "Whether to enable runtime checks on MSVC" ON
                           "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    option(STACK_PROTECTOR "Whether to enable the GCC stack checker while compiling" OFF)
endif()

set(USE_DUMMY_PSEH FALSE CACHE BOOL
"Whether to disable PSEH support.")

set(REACTOS_TARGET_NT "0xA00" CACHE STRING
"Target NT version (e.g. 0x502, 0x600, 0x601, 0xA00)")

set(DLL_EXPORT_VERSION "${REACTOS_TARGET_NT}" CACHE INTERNAL
"The NT version the user mode DLLs target." FORCE)
