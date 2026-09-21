# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/targets.cmake
# PURPOSE:     Optional host-native test build and execution targets
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

get_filename_component(HOST_TEST_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HOST_TEST_BUILD_DIR "${REACTOS_BINARY_DIR}/host-tests" CACHE PATH "Native host test build directory")
set(HOST_TEST_JOBS 4 CACHE STRING "Parallel jobs for native host tests")
if(NOT HOST_TEST_C_COMPILER AND HOST_TOOLS_C_COMPILER)
    set(HOST_TEST_C_COMPILER "${HOST_TOOLS_C_COMPILER}" CACHE FILEPATH "Native host test C compiler")
endif()
find_program(HOST_TEST_C_COMPILER NAMES cc clang gcc NO_CMAKE_FIND_ROOT_PATH)

add_custom_target(host_tests_build
    COMMAND ${CMAKE_COMMAND} -E env
            --unset=CC --unset=CXX --unset=CFLAGS --unset=CXXFLAGS --unset=LDFLAGS
            --unset=CMAKE_TOOLCHAIN_FILE
            ${CMAKE_COMMAND}
            -S ${HOST_TEST_SOURCE_DIR}
            -B ${HOST_TEST_BUILD_DIR} -G Ninja
            -DCMAKE_C_COMPILER:FILEPATH=${HOST_TEST_C_COMPILER}
            -DBUILD_TESTING:BOOL=ON
    COMMAND ${CMAKE_COMMAND} --build ${HOST_TEST_BUILD_DIR} --parallel ${HOST_TEST_JOBS}
    USES_TERMINAL VERBATIM)

add_custom_target(host_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${HOST_TEST_BUILD_DIR}
            --output-on-failure --parallel ${HOST_TEST_JOBS}
    DEPENDS host_tests_build
    USES_TERMINAL VERBATIM)
