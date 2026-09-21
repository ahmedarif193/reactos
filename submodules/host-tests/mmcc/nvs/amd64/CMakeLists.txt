# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/nvs/amd64/CMakeLists.txt
# PURPOSE:     AMD64 memory manager host-test build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)
project(mm_amd64_boot_tests LANGUAGES C)
include(CTest)
add_test(NAME mm_amd64.backend_boundary
    COMMAND ${CMAKE_COMMAND} -DNVS_SOURCE_DIR=${MMCC_KERNEL_ROOT}/nvs
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check-backend.cmake)

foreach(TARGET mm_amd64_boot mm_amd64_boot_asan)
    add_executable(${TARGET} t_boot.c ${MMCC_KERNEL_ROOT}/nvs/arch/amd64/boot.c ${MMCC_KERNEL_ROOT}/nvs/arch/amd64/pte.c)
    target_compile_definitions(${TARGET} PRIVATE MM_HOST_TEST)
    target_include_directories(${TARGET} PRIVATE ${MMCC_KERNEL_ROOT})
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -g -O1)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()
target_compile_options(mm_amd64_boot_asan PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mm_amd64_boot_asan PRIVATE -fsanitize=address,undefined)

foreach(TARGET mm_amd64_pte mm_amd64_pte_asan)
    add_executable(${TARGET} t_pte.c ${MMCC_KERNEL_ROOT}/nvs/arch/amd64/pte.c)
    target_compile_definitions(${TARGET} PRIVATE MM_HOST_TEST)
    target_include_directories(${TARGET} PRIVATE ${MMCC_KERNEL_ROOT})
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -g -O1)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()
target_compile_options(mm_amd64_pte_asan PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mm_amd64_pte_asan PRIVATE -fsanitize=address,undefined)

find_package(Threads REQUIRED)
foreach(TARGET mm_amd64_tlb mm_amd64_tlb_asan mm_amd64_tlb_tsan)
    add_executable(${TARGET} t_tlb.c ${MMCC_KERNEL_ROOT}/nvs/arch/amd64/tlb.c)
    target_compile_definitions(${TARGET} PRIVATE MM_HOST_TEST)
    target_include_directories(${TARGET} PRIVATE ${MMCC_KERNEL_ROOT})
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -g -O1)
    target_link_libraries(${TARGET} PRIVATE Threads::Threads)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    set_tests_properties(${TARGET} PROPERTIES TIMEOUT 60)
endforeach()
target_compile_options(mm_amd64_tlb_asan PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mm_amd64_tlb_asan PRIVATE -fsanitize=address,undefined)
target_compile_options(mm_amd64_tlb_tsan PRIVATE -fsanitize=thread)
target_link_options(mm_amd64_tlb_tsan PRIVATE -fsanitize=thread)
