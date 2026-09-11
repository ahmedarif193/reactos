# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
# Standalone host validation: cmake -S sdk/lib/dwmprof/tests -B <test-build>
cmake_minimum_required(VERSION 3.16)
project(dwmprof_counter_tests C)
find_package(Threads REQUIRED)
enable_testing()
add_executable(counter_core counter_core.c)
target_include_directories(counter_core PRIVATE ../../../include/reactos)
target_link_libraries(counter_core PRIVATE Threads::Threads)
add_test(NAME counter_core COMMAND counter_core)
set_tests_properties(counter_core PROPERTIES TIMEOUT 30)
