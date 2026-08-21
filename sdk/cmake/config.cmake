
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

set(KD_DEBUGGER "AUTO" CACHE STRING
"Kernel debugger mode. Specify one of:
 AUTO NONE KDBG EXTERNAL")
set_property(CACHE KD_DEBUGGER PROPERTY STRINGS AUTO NONE KDBG EXTERNAL)

set(KD_DEFAULT_TRANSPORT "KDCOM" CACHE STRING
"Default transport for the external KD protocol. Specify one of:
 KDCOM KDGDB")
set_property(CACHE KD_DEFAULT_TRANSPORT PROPERTY STRINGS KDCOM KDGDB)

set(ENABLE_KD_WATCHDOG FALSE CACHE BOOL
"Whether to enable the KD log-stall watchdog by default")

set(KD_WATCHDOG_TIMEOUT "6" CACHE STRING
"Default KD log-stall watchdog timeout in seconds when enabled")
if(NOT KD_WATCHDOG_TIMEOUT MATCHES "^[0-9]+$" OR
   KD_WATCHDOG_TIMEOUT LESS 5 OR KD_WATCHDOG_TIMEOUT GREATER 3600)
    message(FATAL_ERROR
        "KD_WATCHDOG_TIMEOUT must be an integer between 5 and 3600 seconds")
endif()
if(ENABLE_KD_WATCHDOG)
    set(_KD_LOG_WATCHDOG_DEFAULT_SECONDS ${KD_WATCHDOG_TIMEOUT})
else()
    set(_KD_LOG_WATCHDOG_DEFAULT_SECONDS 0)
endif()

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

string(TOUPPER "${KD_DEBUGGER}" _REACTOS_KD_DEBUGGER)
if(NOT _REACTOS_KD_DEBUGGER MATCHES "^(AUTO|NONE|KDBG|EXTERNAL)$")
    message(FATAL_ERROR
        "Unknown KD_DEBUGGER '${KD_DEBUGGER}'; expected AUTO, NONE, KDBG or EXTERNAL")
endif()

string(TOUPPER "${KD_DEFAULT_TRANSPORT}" _REACTOS_KD_DEFAULT_TRANSPORT)
if(NOT _REACTOS_KD_DEFAULT_TRANSPORT MATCHES "^(KDCOM|KDGDB)$")
    message(FATAL_ERROR
        "Unknown KD_DEFAULT_TRANSPORT '${KD_DEFAULT_TRANSPORT}'; expected KDCOM or KDGDB")
endif()

if(_REACTOS_KD_DEBUGGER STREQUAL "NONE")
    set(KDBG FALSE)
    set(_WINKD_ FALSE)
    set(GDB FALSE)
elseif(_REACTOS_KD_DEBUGGER STREQUAL "KDBG")
    set(KDBG TRUE)
    set(_WINKD_ FALSE)
    set(GDB FALSE)
elseif(_REACTOS_KD_DEBUGGER STREQUAL "EXTERNAL")
    set(KDBG FALSE)
    set(_WINKD_ TRUE)
    if(_REACTOS_KD_DEFAULT_TRANSPORT STREQUAL "KDGDB")
        set(GDB TRUE)
    else()
        set(GDB FALSE)
    endif()
endif()

if(GDB)
    if(NOT (ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64"))
        message(FATAL_ERROR "KDGDB is only supported on i386, amd64 and arm64")
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

option(ROSSYM_COMPRESSION "Whether to compress the embedded .rossym symbol section." OFF)

cmake_dependent_option(ENABLE_FEX_ARM64EC
                       "Whether to build the optional FEX ARM64EC emulator for running AMD64 binaries on ARM64." OFF
                       "ARCH STREQUAL arm64" OFF)

cmake_dependent_option(ENABLE_FEX_ARM64EC_TEST_PAYLOADS
                       "Whether to import optional AMD64 diagnostic executables into FEX ARM64EC images." OFF
                       "ARCH STREQUAL arm64 AND ENABLE_FEX_ARM64EC" OFF)

# Set only by the nested build configured from arm64ec.cmake. ARCH deliberately
# remains arm64 so source selection and the kernel architecture never become a
# new global ARM64EC target; the toolchain and user-mode ABI are switched here.
cmake_dependent_option(ARM64EC_RUNTIME
                       "Whether this ARM64 build provides the ARM64EC user runtime for FEX." OFF
                       "ARCH STREQUAL arm64" OFF)

cmake_dependent_option(ENABLE_WOW64 "Whether to build the amd64 WoW64 subsystem." OFF
                       "ARCH STREQUAL amd64" OFF)

cmake_dependent_option(ENABLE_ROSV
                       "Whether to build the ROSV VMX hypervisor driver and its user-mode tools." OFF
                       "ARCH STREQUAL amd64" OFF)

cmake_dependent_option(FREELDR_HTTP_BOOT
                       "Whether to build the FreeLdr UEFI HTTP boot path." OFF
                       "LATTEPANDAMU_SUPPORT OR RPI_SUPPORT" OFF)

cmake_dependent_option(ENABLE_ROSAUTOTEST_BOOT_RUN
                       "Whether to run the full RosAutoTest suite automatically at boot." OFF
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

cmake_dependent_option(ENABLE_CPUBENCH_BOOT_RUN
                       "Whether to run CPUbench automatically at boot." OFF
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

cmake_dependent_option(ENABLE_KMTEST_BOOT_RUN
                       "Whether to run unattended KMTests automatically at boot." OFF
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

cmake_dependent_option(ENABLE_USB_STORAGE_BOOT_BENCHMARK
                       "Whether to run matched kernel- and user-mode USB storage read benchmarks at boot." OFF
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

cmake_dependent_option(ENABLE_RP1GEM_BENCHMARK
                       "Whether to run the single-stream RP1 GEM benchmark from ARM64 HTTP boot." OFF
                       "ARCH STREQUAL arm64 AND CMAKE_BUILD_TYPE STREQUAL Debug AND RPI_SUPPORT AND FREELDR_HTTP_BOOT" OFF)

cmake_dependent_option(ENABLE_RPI5_WIFI_BOOT_RUN
                       "Whether to run the RPi5 Wi-Fi scan automatically at boot." OFF
                       "ARCH STREQUAL arm64 AND CMAKE_BUILD_TYPE STREQUAL Debug AND RPI_SUPPORT" OFF)

if(ENABLE_ROSAUTOTEST_BOOT_RUN OR
   ENABLE_CPUBENCH_BOOT_RUN OR
   ENABLE_KMTEST_BOOT_RUN OR
   ENABLE_USB_STORAGE_BOOT_BENCHMARK OR
   ENABLE_RP1GEM_BENCHMARK OR
   ENABLE_RPI5_WIFI_BOOT_RUN)
    set(ENABLE_BOOT_TEST_RUN TRUE)
else()
    set(ENABLE_BOOT_TEST_RUN FALSE)
endif()

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
