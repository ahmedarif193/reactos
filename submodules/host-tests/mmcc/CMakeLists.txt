# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/CMakeLists.txt
# PURPOSE:     Memory and cache manager host-test configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

set(MMCC_KERNEL_ROOT "${REACTOS_HOST_SOURCE_ROOT}/ntoskrnl")
include_directories(${MMCC_KERNEL_ROOT} ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_subdirectory(nvs/core)
add_subdirectory(nvs/amd64)
add_subdirectory(nvs/arm64)
add_subdirectory(pool)
add_subdirectory(cc)

add_test(NAME nvs.standalone
    COMMAND ${CMAKE_COMMAND} -DNVS_KERNEL_ROOT=${MMCC_KERNEL_ROOT}
            -P ${MMCC_KERNEL_ROOT}/nvs/check-standalone.cmake)
add_test(NAME nvs.neutral
    COMMAND ${CMAKE_COMMAND} -DNVS_SOURCE_DIR=${MMCC_KERNEL_ROOT}/nvs
            -P ${CMAKE_CURRENT_SOURCE_DIR}/nvs/check-neutral.cmake)
add_test(NAME nvs.backends
    COMMAND ${CMAKE_COMMAND} -DNVS_SOURCE_DIR=${MMCC_KERNEL_ROOT}/nvs
            -P ${CMAKE_CURRENT_SOURCE_DIR}/nvs/check-backends.cmake)

foreach(RETIRED mm vmm vmm_backup cc_backup)
    add_test(NAME nvs.reject_${RETIRED}
        COMMAND ${CMAKE_COMMAND} -DNVS_CHECK_SOURCES=${MMCC_KERNEL_ROOT}/${RETIRED}/retired.c
                -P ${MMCC_KERNEL_ROOT}/nvs/check-standalone.cmake)
    set_tests_properties(nvs.reject_${RETIRED} PROPERTIES WILL_FAIL TRUE)
endforeach()
