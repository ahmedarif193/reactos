/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device object security routines provided by wdmsec.lib
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>

NTKERNELAPI
NTSTATUS
NTAPI
IoCreateDeviceSecure(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ ULONG DeviceExtensionSize,
    _In_opt_ PUNICODE_STRING DeviceName,
    _In_ DEVICE_TYPE DeviceType,
    _In_ ULONG DeviceCharacteristics,
    _In_ BOOLEAN Exclusive,
    _In_ PCUNICODE_STRING DefaultSDDLString,
    _In_opt_ LPCGUID DeviceClassGuid,
    _Out_ PDEVICE_OBJECT *DeviceObject);

#define WDMSEC_SDDL(_Name, _String) \
    static const WCHAR _Name##_Buffer[] = _String; \
    const UNICODE_STRING _Name = \
    { \
        sizeof(_Name##_Buffer) - sizeof(WCHAR), \
        sizeof(_Name##_Buffer), \
        (PWSTR)_Name##_Buffer \
    }

WDMSEC_SDDL(SDDL_DEVOBJ_KERNEL_ONLY, L"D:P");
WDMSEC_SDDL(SDDL_DEVOBJ_SYS_ALL, L"D:P(A;;GA;;;SY)");
WDMSEC_SDDL(SDDL_DEVOBJ_SYS_ALL_ADM_ALL, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
WDMSEC_SDDL(SDDL_DEVOBJ_SYS_ALL_ADM_RX, L"D:P(A;;GA;;;SY)(A;;GRGX;;;BA)");
WDMSEC_SDDL(SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_R,
            L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GR;;;WD)");
WDMSEC_SDDL(SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_R_RES_R,
            L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GR;;;WD)(A;;GR;;;RC)");
WDMSEC_SDDL(SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R,
            L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GRGW;;;WD)(A;;GR;;;RC)");
WDMSEC_SDDL(SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX,
            L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GRGWGX;;;WD)(A;;GRGWGX;;;RC)");

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
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    return IoCreateDeviceSecure(DriverObject,
                                DeviceExtensionSize,
                                DeviceName,
                                DeviceType,
                                DeviceCharacteristics,
                                Exclusive,
                                DefaultSDDLString,
                                DeviceClassGuid,
                                DeviceObject);
}

NTSTATUS
NTAPI
WdmlibRtlInitUnicodeStringEx(
    _Out_ PUNICODE_STRING DestinationString,
    _In_opt_ PCWSTR SourceString)
{
    return RtlInitUnicodeStringEx(DestinationString, SourceString);
}

NTSTATUS
NTAPI
WdmlibIoValidateDeviceIoControlAccess(
    _In_ PIRP Irp,
    _In_ ULONG RequiredAccess)
{
    return IoValidateDeviceIoControlAccess(Irp, RequiredAccess);
}
