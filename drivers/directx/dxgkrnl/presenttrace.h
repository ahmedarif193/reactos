/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#pragma once
#define DPT_KERNEL
#include <reactos/dwmpresenttracecore.h>
extern DPT_BANK g_DxgPresentTrace;
DPT_SCOPE DxgPresentTraceContextBegin(ULONG Pid);
VOID DxgPresentTraceContextEnd(DPT_SCOPE Scope, ULONG Node, ULONG Kind);
LONG DxgPresentTraceControl(const DPT_REQUEST *Request, DPT_DOMAIN *Output, ULONG Bytes);
VOID DxgPresentTracePacket(ULONG Pid, ULONG Node, ULONG Fence,
                          DPT_SCOPE Queue, DPT_SCOPE Dispatch, BOOLEAN Complete,
                          ULONGLONG Visit, ULONGLONG Claim, ULONGLONG RootTicks);
