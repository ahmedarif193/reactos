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

add_custom_command(
    OUTPUT ${GEN_SYSCALLS_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOOLS_FOLDER}"
    COMMAND ${GEN_SYSCALLS_CARGO_EXECUTABLE} build
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
