/*
 * PROJECT:     LiberNT storage read benchmark
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared protocol for kernel- and user-mode raw read measurements
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define STORAGE_READ_BENCHMARK_BLOCK_SIZE   (128UL * 1024)
#define STORAGE_READ_BENCHMARK_OFFSET       (128ULL * 1024 * 1024)
#define STORAGE_READ_BENCHMARK_LENGTH       (512ULL * 1024 * 1024)
#define STORAGE_READ_BENCHMARK_USER_RUNS    5UL
