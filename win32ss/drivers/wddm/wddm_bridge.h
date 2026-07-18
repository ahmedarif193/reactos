/*
 * PROJECT:     LiberNT Display Driver Model - Win32k/dxgkrnl Bridge
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Public interface for the win32k ↔ dxgkrnl bridge
 * COPYRIGHT:   Copyright 2025 LiberNT Contributors
 */

#pragma once

#include <reactos/rddm/rxgkinterface.h>

/* Bridge lifecycle */
NTSTATUS WddmBridgeInit(VOID);
VOID     WddmBridgeCleanup(VOID);
NTSTATUS WddmBridgeGetStatus(VOID);
NTSTATUS WddmBridgeRequireReady(VOID);
BOOLEAN  WddmBridgeIsReady(VOID);
ULONG    WddmBridgeGetInterfaceVersion(VOID);
NTSTATUS WddmBridgeGetInterface(_Out_ PREACTOS_WIN32K_DXGKRNL_INTERFACE Interface);

/* Kernel-to-kernel IOCTL helper */
NTSTATUS
WddmBridgeSendIoctl(
    _In_      ULONG  IoControlCode,
    _In_opt_  PVOID  InputBuffer,
    _In_      ULONG  InputSize,
    _Out_opt_ PVOID  OutputBuffer,
    _In_      ULONG  OutputSize);

NTSTATUS
WddmBridgeSendIoctlWithInformation(
    _In_      ULONG      IoControlCode,
    _In_opt_  PVOID      InputBuffer,
    _In_      ULONG      InputSize,
    _Out_opt_ PVOID      OutputBuffer,
    _In_      ULONG      OutputSize,
    _Out_opt_ PULONG_PTR Information);

/* Cached device object pointer (set by WddmBridgeInit) */
extern PFILE_OBJECT   g_DxgkrnlFileObject;
extern PDEVICE_OBJECT g_DxgkrnlDeviceObject;
