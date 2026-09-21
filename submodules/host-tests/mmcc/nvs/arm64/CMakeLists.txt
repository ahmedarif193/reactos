# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/nvs/arm64/CMakeLists.txt
# PURPOSE:     ARM64 memory manager host-test build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)
project(mm_arm64_backend_tests LANGUAGES C)
include(CTest)
find_package(Threads REQUIRED)

foreach(TARGET mm_arm64_tlb mm_arm64_tlb_asan mm_arm64_tlb_tsan)
    add_executable(${TARGET} t_tlb.c ${MMCC_KERNEL_ROOT}/nvs/arch/arm64/tlb.c)
    target_compile_definitions(${TARGET} PRIVATE MM_HOST_TEST)
    target_include_directories(${TARGET} PRIVATE ${MMCC_KERNEL_ROOT})
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -g -O1)
    target_link_libraries(${TARGET} PRIVATE Threads::Threads)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    set_tests_properties(${TARGET} PROPERTIES TIMEOUT 60)
endforeach()
target_compile_options(mm_arm64_tlb_asan PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mm_arm64_tlb_asan PRIVATE -fsanitize=address,undefined)
target_compile_options(mm_arm64_tlb_tsan PRIVATE -fsanitize=thread)
target_link_options(mm_arm64_tlb_tsan PRIVATE -fsanitize=thread)
