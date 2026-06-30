/*
 * PROJECT:     ReactOS DirectX Graphics Kernel (dxgkrnl.sys)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Public declarations for adapter.c
 *              (miniport registration and adapter lifecycle)
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * Include this header from dxgkrnl source files that need to call the
 * adapter subsystem API.  Miniport drivers do NOT include this header;
 * they only call DxgkInitialize / DxgkInitializeEx which are exported
 * from dxgkrnl.sys.
 */

#ifndef _DXGKRNL_ADAPTER_H_
#define _DXGKRNL_ADAPTER_H_

/*
 * All types referenced below are declared in dxgkrnl_private.h.
 * adapter.h only adds the public function prototypes; it does not
 * re-declare any structures.
 */
#include "dxgkrnl_private.h"

/* =========================================================================
 * Miniport entry-point exports
 *
 * These two functions are the only symbols that miniport drivers import
 * from dxgkrnl.sys.  They are declared with APIENTRY (__stdcall on x86,
 * default calling convention on x64) to match the WDK contract.
 * ========================================================================= */

/**
 * DxgkInitialize
 *
 * Called from a WDDM miniport's DriverEntry routine.
 * Stores the miniport's callback table (DriverInitializationData) in a
 * per-driver-object extension and hooks the DriverObject so that PnP and
 * Power IRPs are routed through dxgkrnl.
 *
 * @param DriverObject
 *   The DRIVER_OBJECT passed to the miniport's DriverEntry.
 *
 * @param RegistryPath
 *   The service registry path passed to the miniport's DriverEntry.
 *
 * @param DriverInitializationData
 *   Pointer to a DRIVER_INITIALIZATION_DATA filled in by the miniport.
 *   Version must be >= DXGKDDI_INTERFACE_VERSION_VISTA (0x1052).
 *
 * @return STATUS_SUCCESS on success, an error NTSTATUS otherwise.
 */
NTSTATUS
APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);

/**
 * DxgkInitializeEx
 *
 * Extended version of DxgkInitialize that also accepts an explicit
 * DriverInitDataSize parameter.  This allows miniports compiled against
 * newer WDK versions (with a larger DRIVER_INITIALIZATION_DATA) to
 * register with an older dxgkrnl without buffer overruns.
 *
 * @param DriverObject
 *   The DRIVER_OBJECT passed to the miniport's DriverEntry.
 *
 * @param RegistryPath
 *   The service registry path passed to the miniport's DriverEntry.
 *
 * @param DriverInitDataSize
 *   sizeof(DRIVER_INITIALIZATION_DATA) as seen by the miniport.
 *
 * @param DriverInitializationData
 *   Pointer to a DRIVER_INITIALIZATION_DATA filled in by the miniport.
 *
 * @return STATUS_SUCCESS on success, an error NTSTATUS otherwise.
 */
NTSTATUS
APIENTRY
DxgkInitializeEx(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ ULONG                       DriverInitDataSize,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);

/* =========================================================================
 * Per-adapter lifecycle API
 *
 * Called by the PnP dispatcher (adapter.c internal) and potentially by
 * other dxgkrnl subsystems (scheduler, VidPN manager, etc.).
 * ========================================================================= */

/**
 * DxgkAdapterStart
 *
 * Called from the IRP_MN_START_DEVICE handler after the lower driver has
 * successfully processed the IRP.  Populates a DXGK_INTERFACE (the set of
 * dxgkrnl callbacks provided to the miniport) and invokes the miniport's
 * DxgkDdiStartDevice callback.
 *
 * @param Adapter
 *   The per-adapter context allocated by DxgkpAddDevice.
 *
 * @param AllocatedResources
 *   Raw hardware resource list from the PnP stack location.
 *
 * @param TranslatedResources
 *   Translated hardware resource list from the PnP stack location.
 *
 * @return STATUS_SUCCESS if the miniport started successfully.
 */
NTSTATUS
DxgkAdapterStart(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PCM_RESOURCE_LIST AllocatedResources,
    _In_ PCM_RESOURCE_LIST TranslatedResources);

/**
 * DxgkAdapterStop
 *
 * Called from the IRP_MN_STOP_DEVICE handler.  Notifies the miniport via
 * DxgkDdiStopDevice and marks the adapter as stopped.
 *
 * @param Adapter
 *   The per-adapter context to stop.
 *
 * @return STATUS_SUCCESS (errors from the miniport are logged but ignored).
 */
NTSTATUS
DxgkAdapterStop(
    _In_ PDXGKRNL_ADAPTER Adapter);

/**
 * DxgkAdapterRemove
 *
 * Called from the IRP_MN_REMOVE_DEVICE handler.  Calls the miniport's
 * DxgkDdiRemoveDevice, removes the adapter from all tracking lists,
 * detaches from the device stack, and deletes the FDO.
 *
 * After this call returns, the Adapter pointer is invalid.
 *
 * @param Adapter
 *   The per-adapter context to remove.
 */
VOID
DxgkAdapterRemove(
    _In_ PDXGKRNL_ADAPTER Adapter);

/* =========================================================================
 * dxgkrnl-to-miniport callback exports (DxgkCb*)
 *
 * These are placed into the DXGK_INTERFACE structure that dxgkrnl passes
 * to the miniport's DxgkDdiStartDevice.  They are NOT exported from the
 * driver image; the miniport only calls them through the interface struct.
 * ========================================================================= */

NTSTATUS APIENTRY DxgkCbNotifyInterrupt(
    _In_ HANDLE                           DeviceHandle,
    _In_ PDXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterruptData);

NTSTATUS APIENTRY DxgkCbNotifyDpc(
    _In_ HANDLE DeviceHandle);

NTSTATUS APIENTRY DxgkCbGetDeviceInformation(
    _In_  HANDLE            DeviceHandle,
    _Out_ PDXGK_DEVICE_INFO DeviceInformation);

NTSTATUS APIENTRY DxgkCbAllocateContiguousMemory(
    _In_    HANDLE                             DeviceHandle,
    _Inout_ PDXGKARGCB_ALLOCATECONTIGUOUSMEMORY AllocContiguousMemory);

NTSTATUS APIENTRY DxgkCbFreeContiguousMemory(
    _In_ HANDLE                          DeviceHandle,
    _In_ PDXGKARGCB_FREECONTIGUOUSMEMORY FreeContiguousMemory);

NTSTATUS APIENTRY DxgkCbMapPhysicalMemory(
    _In_    HANDLE                       DeviceHandle,
    _Inout_ PDXGKARGCB_MAPPHYSICALMEMORY MapPhysicalMemory);

NTSTATUS APIENTRY DxgkCbUnmapPhysicalMemory(
    _In_ HANDLE                           DeviceHandle,
    _In_ PDXGKARGCB_UNMAP_PHYSICAL_MEMORY UnmapPhysicalMemory);

NTSTATUS APIENTRY DxgkCbIndicateChildStatus(
    _In_ HANDLE             DeviceHandle,
    _In_ PDXGK_CHILD_STATUS ChildStatus);

NTSTATUS APIENTRY DxgkCbSynchronizeExecution(
    _In_  HANDLE                  DeviceHandle,
    _In_  PKSYNCHRONIZE_ROUTINE   SynchronizeRoutine,
    _In_  PVOID                   Context,
    _In_  ULONG                   MessageNumber,
    _Out_ PBOOLEAN                ReturnValue);

NTSTATUS APIENTRY DxgkCbAcquirePostDisplayOwnership(
    _In_  HANDLE                        DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION     DisplayInformation);

/* =========================================================================
 * Global adapter list accessors
 * ========================================================================= */

extern KSPIN_LOCK DxgkAdapterGlobalListLock;
extern LIST_ENTRY DxgkAdapterGlobalListHead;

#endif /* _DXGKRNL_ADAPTER_H_ */
