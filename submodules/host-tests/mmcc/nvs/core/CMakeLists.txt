# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/nvs/core/CMakeLists.txt
# PURPOSE:     Shared memory manager host-test build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)
project(mm_core_host_tests LANGUAGES C)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(NVS_ROOT ${MMCC_KERNEL_ROOT}/nvs)

set(MM_CORE
    ${NVS_ROOT}/core/pfndb.c
    ${NVS_ROOT}/core/pagetable.c
    ${NVS_ROOT}/core/mmsystem.c
    ${NVS_ROOT}/core/adopt.c
    ${NVS_ROOT}/pagefile/pagefile.c
    ${NVS_ROOT}/fault/fault.c
    ${NVS_ROOT}/vm/vmops.c
    ${NVS_ROOT}/vm/large.c
    ${NVS_ROOT}/vm/awe.c
    ${NVS_ROOT}/ws/trim.c
    ${NVS_ROOT}/section/section.c
    ${NVS_ROOT}/sys/sysregion.c
    ${NVS_ROOT}/sys/syspte.c
    ${NVS_ROOT}/sys/kstack.c
    ${NVS_ROOT}/sys/iospace.c
    ${NVS_ROOT}/sys/mdl.c
    ${NVS_ROOT}/proc/process.c
    ${NVS_ROOT}/proc/clone.c
    ${NVS_ROOT}/vad/vadtree.c)

set(CC_ROOT ${NVS_ROOT}/../cc)
set(CC_ENGINE
    ${CC_ROOT}/engine/viewpool.c
    ${CC_ROOT}/engine/cachemap.c
    ${CC_ROOT}/engine/dirty.c
    ${CC_ROOT}/engine/bcb.c
    ${CC_ROOT}/engine/copy.c
    ${CC_ROOT}/engine/readahead.c)

set(MM_TESTS mmharness.c t_core.c t_vm.c t_fault_smp.c t_section.c t_stress.c t_ccmm.c t_ntfile.c t_writeback.c t_ntpaging.c t_vad.c t_sys.c t_proc.c ../machine/machine.c ${CC_ENGINE} ${CC_ROOT}/api/ccclose.c ${CC_ROOT}/backing/mmbacking.c ${NVS_ROOT}/nt/ntfileio.c ${NVS_ROOT}/nt/ntpagingio.c)
list(APPEND MM_TESTS t_async.c t_large.c t_awe.c t_clone.c t_session.c ${NVS_ROOT}/nt/ntsession.c ${CC_ROOT}/api/ccflush.c)
list(APPEND MM_TESTS ${NVS_ROOT}/nt/ntprotect.c)
list(APPEND MM_TESTS ${NVS_ROOT}/nt/ntmapping.c)
set(MM_AREAS pfn pagetable vad vm large awe clone session fault fault_smp paging section image stress ccmm ntfile writeback ntpaging async sys process)

include(${NVS_ROOT}/check-standalone.cmake)
nvs_assert_standalone(${MM_CORE} ${CC_ENGINE})

function(mm_test_variant NAME ARCH MACHINE)
    add_executable(${NAME} ${MM_TESTS} ${MM_CORE}
        ${NVS_ROOT}/arch/${ARCH}/pte.c ${NVS_ROOT}/arch/${ARCH}/descriptor.c)
    target_compile_definitions(${NAME} PRIVATE MM_HOST_TEST CC_HOST_TEST ${MACHINE} ${ARGN})
    target_include_directories(${NAME} PRIVATE ${NVS_ROOT}/.. ${NVS_ROOT}/arch/${ARCH})
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
    find_package(Threads REQUIRED)
    target_link_libraries(${NAME} PRIVATE Threads::Threads)
    foreach(AREA ${MM_AREAS})
        add_test(NAME ${NAME}.${AREA} COMMAND ${NAME} ${AREA})
    endforeach()
endfunction()

mm_test_variant(mm_arm64 arm64 MACHINE_ARM64)
target_compile_options(mm_arm64 PRIVATE -O2)

mm_test_variant(mm_amd64 amd64 MACHINE_AMD64)
target_compile_options(mm_amd64 PRIVATE -O2)

mm_test_variant(mm_arm64_asan arm64 MACHINE_ARM64 TEST_LIGHT)
target_compile_options(mm_arm64_asan PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mm_arm64_asan PRIVATE -fsanitize=address,undefined)

mm_test_variant(mm_arm64_tsan arm64 MACHINE_ARM64 TEST_LIGHT)
target_compile_options(mm_arm64_tsan PRIVATE -O1 -fsanitize=thread)
target_link_options(mm_arm64_tsan PRIVATE -fsanitize=thread)

mm_test_variant(mm_amd64_asan amd64 MACHINE_AMD64 TEST_LIGHT)
target_compile_options(mm_amd64_asan PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mm_amd64_asan PRIVATE -fsanitize=address,undefined)

mm_test_variant(mm_amd64_tsan amd64 MACHINE_AMD64 TEST_LIGHT)
target_compile_options(mm_amd64_tsan PRIVATE -O1 -fsanitize=thread)
target_link_options(mm_amd64_tsan PRIVATE -fsanitize=thread)
