/*
 * VideoPort driver — WDDM detection and handoff helpers
 *
 * Copyright (C) 2024 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * Detection mechanism:
 *
 * Both videoprt.sys (XDDM) and dxgkrnl.sys (WDDM) allocate a driver-object
 * extension via IoAllocateDriverObjectExtension using the DriverObject pointer
 * as the unique ID key.  The first ULONG of each extension encodes the driver
 * model version:
 *
 *   XDDM: VIDEO_PORT_DRIVER_EXTENSION::InitializationData.HwInitDataSize
 *         Values are ~0x68 (NT4), ~0xB8 (W2K), ~0x190 (XP).  Always < 0x1052.
 *
 *   WDDM: DXGKRNL_MINIPORT_CONTEXT::InitData.Version
 *         DXGKDDI_INTERFACE_VERSION_VISTA = 0x1052, higher for later Windows.
 *
 * Any extension whose first ULONG is >= DXGK_DDI_VERSION_WDDM_MIN (0x1052)
 * belongs to a WDDM miniport.
 */

#include "videoprt.h"

#define NDEBUG
#include <debug.h>

/*
 * Minimum DXGKDDI interface version that indicates a WDDM miniport.
 * This is DXGKDDI_INTERFACE_VERSION_VISTA = 0x1052 from dxgddi.h.
 */
#define DXGK_DDI_VERSION_WDDM_MIN  0x1052UL

/*
 * VidPortIsWddmDriver
 *
 * Returns TRUE if the given driver object belongs to a WDDM miniport
 * (one that called DxgkInitialize rather than VideoPortInitialize).
 *
 * The detection reads the first ULONG from the driver-object extension
 * allocated by whichever port driver called IoAllocateDriverObjectExtension.
 * XDDM values are always below DXGK_DDI_VERSION_WDDM_MIN; WDDM values
 * are always >= DXGK_DDI_VERSION_WDDM_MIN.
 */
BOOLEAN
NTAPI
VidPortIsWddmDriver(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PVOID Extension;
    ULONG Version;

    PAGED_CODE();

    if (DriverObject == NULL)
        return FALSE;

    /* Retrieve whatever extension was allocated with the DriverObject as key */
    Extension = IoGetDriverObjectExtension(DriverObject, DriverObject);
    if (Extension == NULL)
        return FALSE;

    /*
     * Both VIDEO_PORT_DRIVER_EXTENSION (XDDM) and DXGKRNL_MINIPORT_CONTEXT
     * (WDDM) start with a ULONG that encodes the version/size. Read it
     * without any struct assumptions via a raw pointer cast — this is safe
     * because the allocation is at least sizeof(ULONG) bytes.
     */
    Version = *(ULONG UNALIGNED *)Extension;

    DPRINT("VidPortIsWddmDriver: DriverObject %p Extension %p FirstUlong=0x%lx\n",
           DriverObject, Extension, Version);

    return (Version >= DXGK_DDI_VERSION_WDDM_MIN);
}

/*
 * VidPortHandoffToWddm
 *
 * Called when videoprt's AddDevice detects a WDDM miniport.  Returns
 * STATUS_NOT_SUPPORTED so PnP will skip videoprt and try dxgkrnl instead.
 *
 * MiniportDeviceContext is unused — it is present only for future extension.
 */
NTSTATUS
NTAPI
VidPortHandoffToWddm(
    _In_opt_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    DPRINT("VidPortHandoffToWddm: yielding to WDDM stack\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * VidPortCheckWddmFdoPresent
 *
 * Returns TRUE if dxgkrnl.sys has already created an FDO on top of the
 * given PDO.  This prevents videoprt from double-attaching.
 *
 * Implementation: call IoGetAttachedDeviceReference to get the topmost
 * device in the stack, then check whether that topmost device is different
 * from the PDO itself and is owned by a driver whose name ends in "dxgkrnl".
 * If the topmost device == PDO, no FDO has been attached yet.
 */
BOOLEAN
NTAPI
VidPortCheckWddmFdoPresent(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT TopDevice;
    PDRIVER_OBJECT TopDriver;
    UNICODE_STRING DriverName;
    UNICODE_STRING DxgkrnlSuffix;
    UNICODE_STRING Tail;
    BOOLEAN Result = FALSE;

    PAGED_CODE();

    if (PhysicalDeviceObject == NULL)
        return FALSE;

    /*
     * IoGetAttachedDeviceReference returns the topmost device in the stack
     * with an additional reference that we must release when done.
     * If it equals the PDO, the stack has not been extended yet.
     */
    TopDevice = IoGetAttachedDeviceReference(PhysicalDeviceObject);
    if (TopDevice == PhysicalDeviceObject)
    {
        ObDereferenceObject(TopDevice);
        return FALSE;
    }

    TopDriver = TopDevice->DriverObject;
    if (TopDriver != NULL)
    {
        DriverName = TopDriver->DriverName;

        /*
         * dxgkrnl registers as "\Driver\dxgkrnl".
         * Check for the "dxgkrnl" suffix (case-insensitive) to avoid
         * a substring search over the full name.
         */
        RtlInitUnicodeString(&DxgkrnlSuffix, L"dxgkrnl");

        if (DriverName.Length >= DxgkrnlSuffix.Length &&
            DriverName.Buffer != NULL)
        {
            Tail.Buffer = (PWCH)((PUCHAR)DriverName.Buffer +
                                 DriverName.Length - DxgkrnlSuffix.Length);
            Tail.Length        = DxgkrnlSuffix.Length;
            Tail.MaximumLength = DxgkrnlSuffix.Length;

            if (RtlEqualUnicodeString(&Tail, &DxgkrnlSuffix, TRUE))
            {
                DPRINT("VidPortCheckWddmFdoPresent: dxgkrnl FDO found on PDO %p (top device %p)\n",
                       PhysicalDeviceObject, TopDevice);
                Result = TRUE;
            }
        }
    }

    ObDereferenceObject(TopDevice);
    return Result;
}

/*
 * VidPortQueryWddmCapableFromRegistry
 *
 * Reads HKLM\SYSTEM\CurrentControlSet\Services\<driver>\WDDMCapable (REG_DWORD).
 * Returns TRUE if the value exists and is non-zero.
 *
 * This is an optional override; the primary detection uses VidPortIsWddmDriver.
 * Setup tools can write WDDMCapable=1 to force the WDDM code path even when
 * the heuristic version comparison is inconclusive.
 */
BOOLEAN
NTAPI
VidPortQueryWddmCapableFromRegistry(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PVIDEO_PORT_DRIVER_EXTENSION DriverExtension;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    NTSTATUS Status;
    ULONG ResultLength;
    KEY_VALUE_PARTIAL_INFORMATION *ValueInfo;
    UCHAR ValueBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    BOOLEAN WddmCapable = FALSE;

    PAGED_CODE();

    if (DriverObject == NULL)
        return FALSE;

    /* Retrieve the driver extension to access the registry path */
    DriverExtension = IoGetDriverObjectExtension(DriverObject, DriverObject);
    if (DriverExtension == NULL)
        return FALSE;

    /*
     * DriverExtension->RegistryPath holds the service key path, e.g.:
     *   \Registry\Machine\System\CurrentControlSet\Services\<miniport>
     */
    if (DriverExtension->RegistryPath.Length == 0 ||
        DriverExtension->RegistryPath.Buffer == NULL)
    {
        return FALSE;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &DriverExtension->RegistryPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("VidPortQueryWddmCapableFromRegistry: ZwOpenKey failed 0x%lx\n", Status);
        return FALSE;
    }

    RtlInitUnicodeString(&ValueName, L"WDDMCapable");

    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueBuffer,
                             sizeof(ValueBuffer),
                             &ResultLength);

    if (NT_SUCCESS(Status))
    {
        ValueInfo = (KEY_VALUE_PARTIAL_INFORMATION *)ValueBuffer;
        if (ValueInfo->Type == REG_DWORD &&
            ValueInfo->DataLength == sizeof(ULONG))
        {
            ULONG DwordValue = *(ULONG *)ValueInfo->Data;
            WddmCapable = (DwordValue != 0);
            DPRINT("VidPortQueryWddmCapableFromRegistry: WDDMCapable = %lu\n", DwordValue);
        }
    }

    ZwClose(KeyHandle);
    return WddmCapable;
}
