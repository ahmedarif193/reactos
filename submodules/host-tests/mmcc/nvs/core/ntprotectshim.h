/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/ntprotectshim.h
 * PURPOSE:     NT memory protection host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "mmharness.h"

#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define PAGE_NOCACHE 0x200
#define PAGE_WRITECOMBINE 0x400
#define PAGE_TARGETS_INVALID 0x40000000

BOOLEAN MiProtectionFromWin32(ULONG Win32Protect, PULONG Protection);
BOOLEAN MiAllocationProtectionFromWin32(ULONG Win32Protect, PULONG Protection);
ULONG MiProtectionToWin32(ULONG Protection);
