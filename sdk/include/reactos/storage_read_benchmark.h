/*
 * PROJECT:     ReactOS storage read benchmark
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared protocol for kernel- and user-mode raw read measurements
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define STORAGE_READ_BENCHMARK_BLOCK_SIZE   (128UL * 1024)
#define STORAGE_READ_BENCHMARK_OFFSET       (128ULL * 1024 * 1024)
#define STORAGE_READ_BENCHMARK_LENGTH       (512ULL * 1024 * 1024)
#define STORAGE_READ_BENCHMARK_USER_RUNS    5UL
