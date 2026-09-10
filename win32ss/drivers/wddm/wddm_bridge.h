/*
 * PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Public interface for the win32k ↔ dxgkrnl bridge
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
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

ULONG_PTR
NTAPI
DxgkEngDispatchNtGdiDdDDI(
    _In_ ULONG NativeOrdinal,
    _In_ ULONG_PTR Argument0,
    _In_ ULONG_PTR Argument1,
    _In_ ULONG_PTR Argument2,
    _In_ ULONG_PTR Argument3,
    _In_ ULONG_PTR Argument4);

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

NTSTATUS
WddmBridgeValidateSharedResourceOwner(
    _In_ const LUID *AdapterLuid,
    _In_ ULONG GlobalShare);

/* Cached device object pointer (set by WddmBridgeInit) */
extern PFILE_OBJECT   g_DxgkrnlFileObject;
extern PDEVICE_OBJECT g_DxgkrnlDeviceObject;
