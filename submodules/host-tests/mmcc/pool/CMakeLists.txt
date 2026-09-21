# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/pool/CMakeLists.txt
# PURPOSE:     Pool allocator host-test build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)
project(pool_host_tests LANGUAGES C)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(POOL_ROOT ${MMCC_KERNEL_ROOT}/nvs/pool)

set(POOL_ENGINE
    ${POOL_ROOT}/va/varegion.c
    ${POOL_ROOT}/heap/heapcore.c
    ${POOL_ROOT}/heap/heapseg.c
    ${POOL_ROOT}/heap/heaplfh.c
    ${POOL_ROOT}/heap/heapvs.c
    ${POOL_ROOT}/heap/heaplarge.c
    ${POOL_ROOT}/heap/heapdebug.c
    ${POOL_ROOT}/track/tagtable.c)

set(POOL_TESTS
    host/harness.c
    host/t_va.c
    host/t_heap_units.c
    host/t_heap_e2e.c
    host/t_track_bench.c)

set(POOL_AREAS va segment lfh vs large heap expansion smp track)

function(pool_test_variant NAME PAGE_SHIFT)
    add_executable(${NAME} ${POOL_TESTS} ${POOL_ENGINE})
    target_compile_definitions(${NAME} PRIVATE POOL_HOST_TEST POOL_HOST_PAGE_SHIFT=${PAGE_SHIFT} ${ARGN})
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-four-char-constants -Wno-multichar -g)
    find_package(Threads REQUIRED)
    target_link_libraries(${NAME} PRIVATE Threads::Threads)
    foreach(AREA ${POOL_AREAS})
        add_test(NAME ${NAME}.${AREA} COMMAND ${NAME} ${AREA})
    endforeach()
endfunction()

pool_test_variant(pool_4k 12)
target_compile_options(pool_4k PRIVATE -O2)

pool_test_variant(pool_16k 14)
target_compile_options(pool_16k PRIVATE -O2)

pool_test_variant(pool_64k 16 TEST_LIGHT)
target_compile_options(pool_64k PRIVATE -O2)

pool_test_variant(pool_asan 12 TEST_LIGHT)
target_compile_options(pool_asan PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(pool_asan PRIVATE -fsanitize=address,undefined)

pool_test_variant(pool_tsan 12 TEST_LIGHT)
target_compile_options(pool_tsan PRIVATE -O1 -fsanitize=thread)
target_link_options(pool_tsan PRIVATE -fsanitize=thread)

foreach(TARGET pool_nt pool_nt_asan)
    add_executable(${TARGET} host/t_ntpool.c ${POOL_ENGINE}
        ${POOL_ROOT}/ex/exinit.c ${POOL_ROOT}/ex/exalloc.c
        ${POOL_ROOT}/ex/exfree.c ${POOL_ROOT}/ex/exquota.c
        ${POOL_ROOT}/backing/mmbacking.c)
    target_compile_definitions(${TARGET} PRIVATE POOL_HOST_TEST)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-multichar -g -O1)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()
target_compile_options(pool_nt_asan PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(pool_nt_asan PRIVATE -fsanitize=address,undefined)
