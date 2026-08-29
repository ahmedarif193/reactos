/*
 * PROJECT:     ReactOS KMDF: driver initialization static library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2021 Max Korostil <mrmks04@yandex.ru>
 */

#include <ntddk.h>
#include <windef.h>
#include "wdf.h"
#include <fxldr.h>


// supplied by the driver this library is linked into
extern
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

const WDFFUNC *WdfFunctions;
PWDF_DRIVER_GLOBALS WdfDriverGlobals;
WDFFUNC WdfDriverMiniportUnloadOverride;

NTSTATUS
FxStubBindClasses(
    _In_ PWDF_BIND_INFO BindInfo);

BOOLEAN
FxStubIsClassBound(VOID);

VOID
FxStubUnbindClasses(
    _In_ PWDF_BIND_INFO BindInfo);

NTSTATUS
FxStubInitTypes(VOID);

#if WDF_STUB_VERSION >= 25

extern ULONG WdfMinimumVersionRequired;

BOOLEAN WdfClientVersionHigherThanFramework;
ULONG WdfFunctionCount = WdfFunctionTableNumEntries;
ULONG WdfStructureCount = Wdf_STRUCTURE_TABLE_NUM_ENTRIES;
WDF_STRUCT_INFO WdfStructures;

WDF_BIND_INFO2 WdfBindInfo =
{
    .V1 =
    {
        .Size = sizeof(WdfBindInfo),
        .Component = L"KmdfLibrary",
        .Version.Major = __WDF_MAJOR_VERSION,
        .Version.Minor = __WDF_MINOR_VERSION,
        .Version.Build = __WDF_BUILD_NUMBER,
        .FuncCount = WdfFunctionTableNumEntries,
        .FuncTable = (WDFFUNC *)&WdfFunctions
    },
    .MinimumVersionRequired = &WdfMinimumVersionRequired,
    .ClientVersionHigherThanFramework = &WdfClientVersionHigherThanFramework,
    .FuncCountPtr = &WdfFunctionCount,
    .StructCountPtr = &WdfStructureCount,
    .StructTable = &WdfStructures
};

#define WDF_BIND_INFO_V1 WdfBindInfo.V1

#else

WDF_BIND_INFO WdfBindInfo =
{
    .Size = sizeof(WdfBindInfo),
    .Component = L"KmdfLibrary",
    .Version.Major = __WDF_MAJOR_VERSION,
    .Version.Minor = __WDF_MINOR_VERSION,
    .Version.Build = __WDF_BUILD_NUMBER,
    .FuncCount = WdfFunctionTableNumEntries,
    .FuncTable = (WDFFUNC *)&WdfFunctions
};

#define WDF_BIND_INFO_V1 WdfBindInfo

#endif

static PDRIVER_UNLOAD pOriginalUnload;
static WCHAR gRegistryPathBuffer[MAX_PATH];
static UNICODE_STRING gRegistryPath =
{
    0,
    sizeof(gRegistryPathBuffer),
    gRegistryPathBuffer
};

static
VOID
FxDriverUnloadCommon(VOID)
{
    FxStubUnbindClasses(&WDF_BIND_INFO_V1);
    WdfVersionUnbind(&gRegistryPath,
                     &WDF_BIND_INFO_V1,
                     (PWDF_COMPONENT_GLOBALS)WdfDriverGlobals);
}

static
VOID
NTAPI
FxDriverMiniportUnload(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(Driver);

    FxDriverUnloadCommon();
}

VOID
NTAPI
FxDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    if (pOriginalUnload != NULL)
    {
        pOriginalUnload(DriverObject);
    }
    FxDriverUnloadCommon();
}

NTSTATUS
NTAPI
FxDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    if (DriverObject == NULL)
    {
        return DriverEntry(DriverObject, RegistryPath);
    }

    // Preserve the service path for the matching unbind operation.
    gRegistryPath.Length = 0;
    RtlCopyUnicodeString(&gRegistryPath, RegistryPath);

    // Bind wdf driver to framework
    status = WdfVersionBind(DriverObject,
                            RegistryPath,
                            &WDF_BIND_INFO_V1,
                            (PWDF_COMPONENT_GLOBALS*)(&WdfDriverGlobals));

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WdfDriverMiniportUnloadOverride =
        WdfFunctions[WdfDriverMiniportUnloadTableIndex];

    status = FxStubBindClasses(&WDF_BIND_INFO_V1);
    if (!NT_SUCCESS(status))
    {
        FxDriverUnloadCommon();
        return status;
    }

    status = FxStubInitTypes();
    if (!NT_SUCCESS(status))
    {
        FxDriverUnloadCommon();
        return status;
    }

    // Call original entry point
    status = DriverEntry(DriverObject, RegistryPath);
    if (!NT_SUCCESS(status))
    {
        FxDriverUnloadCommon();
        return status;
    }

    if (WdfDriverGlobals->DisplaceDriverUnload)
    {
        if (DriverObject->DriverUnload != NULL)
        {
            pOriginalUnload = DriverObject->DriverUnload;
        }
        DriverObject->DriverUnload = FxDriverUnload;
    }
    else if (WdfDriverGlobals->DriverFlags & WdfDriverInitNoDispatchOverride)
    {
        WdfDriverMiniportUnloadOverride = (WDFFUNC)FxDriverMiniportUnload;
    }

    return STATUS_SUCCESS;
}
