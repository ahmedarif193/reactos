# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/ntdll/CMakeLists.txt
# PURPOSE:     NTDLL host-test build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

find_package(Threads REQUIRED)

function(ntdll_cfg_variant NAME)
    add_executable(${NAME} t_cfg.c ${REACTOS_HOST_SOURCE_ROOT}/dll/ntdll/ldr/ldrcfg.c)
    target_include_directories(${NAME} PRIVATE include)
    target_compile_definitions(${NAME} PRIVATE _M_ARM64)
    target_compile_features(${NAME} PRIVATE c_std_11)
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Werror -g)
    target_link_libraries(${NAME} PRIVATE Threads::Threads)
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 20)
endfunction()

ntdll_cfg_variant(ntdll_cfg)
target_compile_options(ntdll_cfg PRIVATE -O2)

ntdll_cfg_variant(ntdll_cfg_asan)
target_compile_options(ntdll_cfg_asan PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(ntdll_cfg_asan PRIVATE -fsanitize=address,undefined)

ntdll_cfg_variant(ntdll_cfg_tsan)
target_compile_options(ntdll_cfg_tsan PRIVATE -O1 -fsanitize=thread)
target_link_options(ntdll_cfg_tsan PRIVATE -fsanitize=thread)
