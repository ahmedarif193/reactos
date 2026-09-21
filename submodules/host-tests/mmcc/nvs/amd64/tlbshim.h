/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/amd64/tlbshim.h
 * PURPOSE:     AMD64 translation lookaside buffer host-test definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/include/mienv.h>

#define MAXIMUM_PROCESSORS 64
#define NTAPI

extern volatile LONG KiTbFlushTimeStamp;
ULONG MiAmd64TlbCpu(VOID);
ULONG64 MiAmd64TlbActiveCpus(VOID);
ULONG64 MiAmd64TlbReadCr3(VOID);
ULONG64 MiAmd64TlbReadCr4(VOID);
VOID MiAmd64TlbWriteCr3(ULONG64 Value);
VOID MiAmd64TlbWriteCr4(ULONG64 Value);
VOID MiAmd64TlbPage(ULONG64 Address);
VOID MiAmd64TlbCpuid(int Registers[4], int Leaf);
VOID MiAmd64TlbPause(VOID);
BOOLEAN MiAmd64TlbDisableInterrupts(VOID);
VOID MiAmd64TlbRestoreInterrupts(BOOLEAN Enabled);
KIRQL MiAmd64TlbRaiseIrql(VOID);
VOID MiAmd64TlbLowerIrql(KIRQL Previous);
VOID MiAmd64TlbPoll(VOID);
VOID MiAmd64TlbInvpcid(VOID);
VOID KeFlushCurrentTb(VOID);
VOID KeFlushEntireTb(BOOLEAN Invalid, BOOLEAN AllProcessors);
