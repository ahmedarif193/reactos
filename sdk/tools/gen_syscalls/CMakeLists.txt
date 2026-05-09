cmake_minimum_required(VERSION 3.17.0)

# Build the gen_syscalls host tool using Rust (cargo).
# This produces a native binary that runs on the build host and
# generates syscall_table.h and ntdll_syscalls.S for the target tree.

set(GEN_SYSCALLS_OUTPUT "${TOOLS_FOLDER}/gen_syscalls")
set(GEN_SYSCALLS_OUTPUT_FILE ${GEN_SYSCALLS_OUTPUT} CACHE FILEPATH "Path to the gen_syscalls host tool" FORCE)
set(GEN_SYSCALLS_BUILD_DIR "${REACTOS_BINARY_DIR}/host-tools/cargo/gen_syscalls")

find_program(GEN_SYSCALLS_CARGO_EXECUTABLE NAMES cargo)
if(NOT GEN_SYSCALLS_CARGO_EXECUTABLE)
    message(FATAL_ERROR "cargo not found: required to build gen_syscalls host tool")
endif()

set(_gen_syscalls_env)
if(DEFINED ROS_CARGO_HOME AND NOT "${ROS_CARGO_HOME}" STREQUAL "")
    list(APPEND _gen_syscalls_env "CARGO_HOME=${ROS_CARGO_HOME}")
endif()
if(DEFINED ROS_RUSTUP_HOME AND NOT "${ROS_RUSTUP_HOME}" STREQUAL "")
    list(APPEND _gen_syscalls_env "RUSTUP_HOME=${ROS_RUSTUP_HOME}")
endif()

set(_gen_syscalls_rustup_toolchain "")
if(DEFINED ROS_RUSTUP_TOOLCHAIN_DEFAULT AND NOT "${ROS_RUSTUP_TOOLCHAIN_DEFAULT}" STREQUAL "")
    set(_gen_syscalls_rustup_toolchain "${ROS_RUSTUP_TOOLCHAIN_DEFAULT}")
elseif(NOT "$ENV{RUSTUP_TOOLCHAIN}" STREQUAL "")
    set(_gen_syscalls_rustup_toolchain "$ENV{RUSTUP_TOOLCHAIN}")
else()
    execute_process(
        COMMAND ${GEN_SYSCALLS_CARGO_EXECUTABLE} --version
        RESULT_VARIABLE _gen_syscalls_cargo_version_rv
        OUTPUT_VARIABLE _gen_syscalls_cargo_version_out
        ERROR_VARIABLE _gen_syscalls_cargo_version_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)

    if((NOT _gen_syscalls_cargo_version_rv EQUAL 0) AND
       (_gen_syscalls_cargo_version_err MATCHES "no default is configured"))
        set(_gen_syscalls_rustup_toolchain "stable")
        message(STATUS "cargo has no rustup default; using RUSTUP_TOOLCHAIN='stable' for gen_syscalls.")
    endif()
endif()

if(NOT "${_gen_syscalls_rustup_toolchain}" STREQUAL "")
    list(APPEND _gen_syscalls_env "RUSTUP_TOOLCHAIN=${_gen_syscalls_rustup_toolchain}")
endif()
set(_gen_syscalls_cargo_cmd ${GEN_SYSCALLS_CARGO_EXECUTABLE})
if(_gen_syscalls_env)
    set(_gen_syscalls_cargo_cmd ${CMAKE_COMMAND} -E env ${_gen_syscalls_env} ${GEN_SYSCALLS_CARGO_EXECUTABLE})
endif()

add_custom_command(
    OUTPUT ${GEN_SYSCALLS_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOOLS_FOLDER}"
    COMMAND ${_gen_syscalls_cargo_cmd} build
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
            --bin gen_syscalls
            --target-dir "${GEN_SYSCALLS_BUILD_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GEN_SYSCALLS_BUILD_DIR}/debug/gen_syscalls"
            "${GEN_SYSCALLS_OUTPUT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
    COMMENT "Building Rust gen_syscalls host tool")

add_custom_target(gen_syscalls ALL
    DEPENDS ${GEN_SYSCALLS_OUTPUT})
