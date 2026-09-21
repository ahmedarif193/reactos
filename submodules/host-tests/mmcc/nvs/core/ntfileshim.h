/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/ntfileshim.h
 * PURPOSE:     NT file I/O host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <mmcc/cc/host/ccntshim.h>

NTSTATUS MiControlRead(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer);
NTSTATUS MiControlWrite(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer);
NTSTATUS MiControlWriteFrames(PVOID Context, ULONG64 Offset, ULONG Length, const ULONG *Frames, ULONG PageCount);
NTSTATUS MiPagingIoFrames(PFILE_OBJECT File, ULONG64 Offset, const ULONG *Frames, ULONG PageCount,
                          BOOLEAN Write, PULONG Transferred);
