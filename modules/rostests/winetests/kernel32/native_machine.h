/*
 * PROJECT:     LiberNT Wine tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Machine tag of the PE image being built
 */

/* PE image tests must use the machine tag of the binary being built. This is
 * an instruction-set property, independent of pointer width. */
#pragma once

#if defined(__i386__) || defined(_M_IX86)
#define WINETEST_IMAGE_FILE_MACHINE IMAGE_FILE_MACHINE_I386
#elif defined(__x86_64__) || defined(_M_AMD64)
#define WINETEST_IMAGE_FILE_MACHINE IMAGE_FILE_MACHINE_AMD64
#elif defined(__arm__) || defined(_M_ARM)
#define WINETEST_IMAGE_FILE_MACHINE IMAGE_FILE_MACHINE_ARMNT
#elif defined(__aarch64__) || defined(_M_ARM64)
#define WINETEST_IMAGE_FILE_MACHINE IMAGE_FILE_MACHINE_ARM64
#elif defined(__powerpc__) || defined(_M_PPC)
#define WINETEST_IMAGE_FILE_MACHINE IMAGE_FILE_MACHINE_POWERPC
#elif defined(__riscv) && (__riscv_xlen == 64)
#define WINETEST_IMAGE_FILE_MACHINE IMAGE_FILE_MACHINE_RISCV64
#else
#error Unsupported PE machine type for the kernel32 image tests
#endif
