/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device object security routines provided by wdmsec.lib
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WDMSEC_H_
#define _WDMSEC_H_

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern const UNICODE_STRING SDDL_DEVOBJ_KERNEL_ONLY;
#define SDDL_DEVOBJ_INF_SUPPLIED SDDL_DEVOBJ_KERNEL_ONLY

extern const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL;
extern const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL_ADM_ALL;
extern const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL_ADM_RX;
extern const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_R;
extern const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_R_RES_R;
extern const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R;
extern const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX;

#undef IoCreateDeviceSecure
#define IoCreateDeviceSecure WdmlibIoCreateDeviceSecure

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
WdmlibIoCreateDeviceSecure(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ ULONG DeviceExtensionSize,
    _In_opt_ PUNICODE_STRING DeviceName,
    _In_ DEVICE_TYPE DeviceType,
    _In_ ULONG DeviceCharacteristics,
    _In_ BOOLEAN Exclusive,
    _In_ PCUNICODE_STRING DefaultSDDLString,
    _In_opt_ LPCGUID DeviceClassGuid,
    _Out_ PDEVICE_OBJECT *DeviceObject);

#undef RtlInitUnicodeStringEx
#define RtlInitUnicodeStringEx WdmlibRtlInitUnicodeStringEx

NTSTATUS
NTAPI
WdmlibRtlInitUnicodeStringEx(
    _Out_ PUNICODE_STRING DestinationString,
    _In_opt_ PCWSTR SourceString);

#undef IoValidateDeviceIoControlAccess
#define IoValidateDeviceIoControlAccess WdmlibIoValidateDeviceIoControlAccess

NTSTATUS
NTAPI
WdmlibIoValidateDeviceIoControlAccess(
    _In_ PIRP Irp,
    _In_ ULONG RequiredAccess);

#ifdef __cplusplus
}
#endif

#endif /* _WDMSEC_H_ */
