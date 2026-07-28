/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     In-kernel virtio-net backend using NETIO/WSK
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include <rosv/rosv.h>

typedef struct _ROSV_VM ROSV_VM, *PROSV_VM;
typedef struct _ROSV_NET_BACKEND_STATE ROSV_NET_BACKEND_STATE, *PROSV_NET_BACKEND_STATE;

NTSTATUS
RosvNetBackendCreate(
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvNetBackendStart(
    _Inout_ PROSV_VM Vm);

VOID
RosvNetBackendStop(
    _Inout_ PROSV_VM Vm);

VOID
RosvNetBackendDestroy(
    _Inout_ PROSV_VM Vm);

VOID
RosvNetBackendKick(
    _In_ PROSV_VM Vm);

BOOLEAN
RosvNetBackendIsActive(
    _In_ PROSV_VM Vm);

VOID
RosvNetBackendQueryStats(
    _In_ PROSV_VM Vm,
    _Out_opt_ PULONG64 TxPackets,
    _Out_opt_ PULONG64 TxBytes,
    _Out_opt_ PULONG64 RxPackets,
    _Out_opt_ PULONG64 RxBytes);
