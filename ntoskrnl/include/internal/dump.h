/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Internal crash dump declarations
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/dump.h>

NTSTATUS NTAPI KeInitializeCrashDumpHeader(_In_ ULONG Type, _In_ ULONG Flags, _Out_writes_bytes_(BufferSize) PVOID Buffer, _In_ ULONG BufferSize, _Out_opt_ PULONG BufferNeeded);
