# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/cc/CMakeLists.txt
# PURPOSE:     Cache manager host-test build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)
project(cc_host_tests LANGUAGES C)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(CC_ROOT ${MMCC_KERNEL_ROOT}/cc)

set(CC_ENGINE
    ${CC_ROOT}/engine/viewpool.c
    ${CC_ROOT}/engine/cachemap.c
    ${CC_ROOT}/engine/dirty.c
    ${CC_ROOT}/engine/bcb.c
    ${CC_ROOT}/engine/copy.c
    ${CC_ROOT}/engine/readahead.c)

set(CC_TESTS host/ccharness.c host/t_engine.c host/t_system.c host/t_ntpin.c host/t_ntcopy.c host/t_ntlayout.c
    ${CC_ROOT}/api/ccpin.c ${CC_ROOT}/api/cccopy.c)
set(CC_AREAS views dirty bcb ntpin ntcopy ntlayout readahead coherency smp)

function(cc_test_variant NAME PAGE_SHIFT)
    add_executable(${NAME} ${CC_TESTS} ${CC_ENGINE})
    target_compile_definitions(${NAME} PRIVATE CC_HOST_TEST CC_HOST_PAGE_SHIFT=${PAGE_SHIFT} ${ARGN})
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
    find_package(Threads REQUIRED)
    target_link_libraries(${NAME} PRIVATE Threads::Threads)
    foreach(AREA ${CC_AREAS})
        add_test(NAME ${NAME}.${AREA} COMMAND ${NAME} ${AREA})
    endforeach()
endfunction()

cc_test_variant(cc_4k 12)
target_compile_options(cc_4k PRIVATE -O2)

cc_test_variant(cc_16k 14)
target_compile_options(cc_16k PRIVATE -O2)

cc_test_variant(cc_64k 16 TEST_LIGHT)
target_compile_options(cc_64k PRIVATE -O2)

cc_test_variant(cc_asan 12 TEST_LIGHT)
target_compile_options(cc_asan PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(cc_asan PRIVATE -fsanitize=address,undefined)

cc_test_variant(cc_tsan 12 TEST_LIGHT)
target_compile_options(cc_tsan PRIVATE -O1 -fsanitize=thread)
target_link_options(cc_tsan PRIVATE -fsanitize=thread)
