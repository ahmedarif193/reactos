#
# Rust Kernel Test Driver Build Configuration
#

# Check if Rust is enabled
if(NOT ENABLE_RUST)
    message(STATUS "Rust disabled, skipping rust_ktest driver")
    return()
endif()

# Determine the Rust target triple based on architecture
if(ARCH STREQUAL "i386")
    set(RUST_TARGET_TRIPLE "i686-pc-windows-gnu")
    set(RUST_TARGET_DIR "i686-pc-windows-gnu")
elseif(ARCH STREQUAL "amd64")
    set(RUST_TARGET_TRIPLE "x86_64-pc-windows-gnu")
    set(RUST_TARGET_DIR "x86_64-pc-windows-gnu")
elseif(ARCH STREQUAL "arm64")
    set(RUST_TARGET_TRIPLE "aarch64-pc-windows-gnu")
    set(RUST_TARGET_DIR "aarch64-pc-windows-gnu")
else()
    message(WARNING "Unsupported architecture for Rust: ${ARCH}")
    return()
endif()

# Set up cargo build directories
set(RUST_KTEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(RUST_KTEST_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/cargo)
set(RUST_KTEST_TARGET_DIR ${RUST_KTEST_BUILD_DIR}/target)

# Determine build profile
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RUST_BUILD_PROFILE "dev")
    set(RUST_BUILD_PROFILE_DIR "debug")
    set(CARGO_BUILD_FLAG "")
else()
    set(RUST_BUILD_PROFILE "release")
    set(RUST_BUILD_PROFILE_DIR "release")
    set(CARGO_BUILD_FLAG "--release")
endif()

# ============================================================================
# Binding Source Options
# ============================================================================

# Always use Microsoft wdk-sys crate (NO WDK installation needed!)
message(STATUS "Rust kernel test driver: Using Microsoft wdk-sys crate")
message(STATUS "  NO WDK installation needed - crates downloaded from crates.io")

# Build features list - enable advanced_tests by default
set(RUST_FEATURE_LIST "advanced_tests")

# Additional features (optional)
if(DEFINED RUST_KTEST_FEATURES)
    list(APPEND RUST_FEATURE_LIST "${RUST_KTEST_FEATURES}")
endif()

# Convert list to comma-separated string for cargo
if(RUST_FEATURE_LIST)
    string(REPLACE ";" "," RUST_FEATURES_STRING "${RUST_FEATURE_LIST}")
    set(CARGO_FEATURES_FLAG "--features" "${RUST_FEATURES_STRING}")
    message(STATUS "Rust kernel test driver features: ${RUST_FEATURES_STRING}")
else()
    set(CARGO_FEATURES_FLAG "")
endif()

# Path to the built Rust static library
set(RUST_KTEST_LIB "${RUST_KTEST_TARGET_DIR}/${RUST_TARGET_DIR}/${RUST_BUILD_PROFILE_DIR}/librust_ktest_lib.a")

# Find cargo
find_program(CARGO_EXECUTABLE cargo HINTS $ENV{CARGO_HOME}/bin)
if(NOT CARGO_EXECUTABLE)
    message(WARNING "cargo not found, skipping rust_ktest driver")
    return()
endif()

# Custom command to build the Rust library
add_custom_command(
    OUTPUT ${RUST_KTEST_LIB}
    COMMAND ${CMAKE_COMMAND} -E env
        CARGO_TARGET_DIR=${RUST_KTEST_TARGET_DIR}
        ${CARGO_EXECUTABLE} build
        ${CARGO_BUILD_FLAG}
        ${CARGO_FEATURES_FLAG}
        --target=${RUST_TARGET_TRIPLE}
        --manifest-path=${RUST_KTEST_SOURCE_DIR}/Cargo.toml
    DEPENDS
        ${RUST_KTEST_SOURCE_DIR}/Cargo.toml
        ${RUST_KTEST_SOURCE_DIR}/src/lib.rs
        ${RUST_KTEST_SOURCE_DIR}/src/wdk_types.rs
        ${RUST_KTEST_SOURCE_DIR}/src/advanced.rs
    WORKING_DIRECTORY ${RUST_KTEST_SOURCE_DIR}
    COMMENT "Building Rust kernel test driver library for ${RUST_TARGET_TRIPLE}"
    VERBATIM
)

# Create custom target for Rust library
add_custom_target(rust_ktest_lib ALL
    DEPENDS ${RUST_KTEST_LIB}
)

# Create the driver module (Rust provides everything)
# We need a dummy C file to satisfy CMake's MODULE requirements
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.c "// Dummy file - real code is in Rust\n")
add_library(rust_ktest MODULE ${CMAKE_CURRENT_BINARY_DIR}/dummy.c)

# Set module type as kernel driver
set_module_type(rust_ktest kernelmodedriver)

# Add dependencies to ensure Rust library is built first
add_dependencies(rust_ktest rust_ktest_lib)

# Link with Rust library AND kernel import libraries
# Use --whole-archive to force inclusion of all Rust code
target_link_options(rust_ktest PRIVATE
    "LINKER:--whole-archive"
    "${RUST_KTEST_LIB}"
    "LINKER:--no-whole-archive"
    "LINKER:--entry=DriverEntry"
)

add_importlibs(rust_ktest ntoskrnl hal)

# Strip debug sections to work around LLVM/Rust PE debug section corruption issue (CORE-16449)
# The debug sections have invalid VirtualAddress fields that cause kernel crashes
if(CMAKE_STRIP)
    add_custom_command(TARGET rust_ktest POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-debug $<TARGET_FILE:rust_ktest>
        COMMENT "Stripping debug sections from rust_ktest.sys (CORE-16449 workaround)"
        VERBATIM
    )
endif()

# Add to install CD
add_cd_file(TARGET rust_ktest DESTINATION reactos/system32/drivers NO_CAB FOR all)

message(STATUS "Rust kernel test driver configured for ${ARCH} (${RUST_TARGET_TRIPLE})")
