/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     XDDM legacy miniport detection and graceful FDO detach.
 *
 *              These routines handle the case where dxgkrnl has already
 *              created an FDO (via DxgkpAddDevice) before discovering that
 *              the underlying miniport is actually an XDDM driver.  The
 *              detection is based on the DXGKRNL_MINIPORT_CONTEXT::InitData
 *              version field — see adapter.c for the threshold value.
 *
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 */

#include "dxgkrnl_private.h"

#define NDEBUG
#include <debug.h>

/*
 * Minimum DXGKDDI interface version that identifies a WDDM miniport.
 * This is DXGKDDI_INTERFACE_VERSION_VISTA = 0x1052 from dxgddi.h.
 * Any version below this value indicates an XDDM miniport.
 */
#define DXGK_DDI_VERSION_WDDM_MIN   0x1052UL

#define DXGKRNL_LIST_ENTRY_IS_LINKED(Entry) \
    (((Entry)->Flink != (Entry)) && ((Entry)->Blink != (Entry)))

/* =========================================================================
 * DxgkLegacyDetect
 *
 * Returns TRUE if the given adapter was created for an XDDM miniport.
 *
 * This function is provided for cases where the XDDM check in DxgkpAddDevice
 * was not reached (e.g., the extension was populated after AddDevice ran).
 * In normal operation DxgkpAddDevice's inline guard is sufficient, but
 * DxgkLegacyDetect can be called from the PnP START_DEVICE path as a
 * belt-and-suspenders check before invoking DxgkDdiStartDevice.
 * ========================================================================= */
BOOLEAN
NTAPI
DxgkLegacyDetect(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;

    PAGED_CODE();

    if (Adapter == NULL)
        return FALSE;

    MpCtx = Adapter->MiniportContext;
    if (MpCtx == NULL)
        return FALSE;

    /*
     * The first ULONG of DXGKRNL_MINIPORT_CONTEXT is InitData.Version.
     * Any value below DXGK_DDI_VERSION_WDDM_MIN indicates an XDDM miniport
     * (either because it called VideoPortInitialize and videoprt allocated
     * the extension, or because it is an NT4/W2K XDDM driver that somehow
     * reached DxgkInitialize).
     */
    DPRINT("DxgkLegacyDetect: adapter %p InitData.Version=0x%lx (min=0x%lx)\n",
           Adapter, MpCtx->InitData.s.Version, DXGK_DDI_VERSION_WDDM_MIN);

    return (MpCtx->InitData.s.Version < DXGK_DDI_VERSION_WDDM_MIN);
}

/* =========================================================================
 * DxgkLegacyDetach
 *
 * Undoes the work done by DxgkpAddDevice for an XDDM miniport that was
 * erroneously set up as a WDDM adapter.  After this call the PDO is free
 * for videoprt.sys to claim.
 *
 * Call sequence:
 *   1. Remove the adapter from both list heads.
 *   2. Detach the FDO from the device stack.
 *   3. Call DxgkDdiRemoveDevice if a miniport device context was created.
 *   4. Delete the FDO.
 *   5. Return STATUS_NOT_SUPPORTED so PnP retries with the next driver.
 * ========================================================================= */
NTSTATUS
NTAPI
DxgkLegacyDetach(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDXGKRNL_ADAPTER            Adapter;
    PDXGKRNL_MINIPORT_CONTEXT   MpCtx;
    KIRQL                       OldIrql;

    PAGED_CODE();

    if (DeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(DeviceObject);
    MpCtx   = Adapter->MiniportContext;

    DPRINT1("DxgkLegacyDetach: detaching XDDM adapter %p (FDO=%p)\n",
            Adapter, DeviceObject);

    /* Mark as removed before touching the lists to prevent re-entry. */
    Adapter->State = DxgkAdapterStateRemoved;

    /* Remove from global adapter list. */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    if (DXGKRNL_LIST_ENTRY_IS_LINKED(&Adapter->GlobalAdapterListEntry))
    {
        RemoveEntryList(&Adapter->GlobalAdapterListEntry);
        InitializeListHead(&Adapter->GlobalAdapterListEntry);
    }
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);

    /* Remove from per-miniport adapter list. */
    if (MpCtx != NULL)
    {
        KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
        if (DXGKRNL_LIST_ENTRY_IS_LINKED(&Adapter->MiniportAdapterListEntry))
        {
            RemoveEntryList(&Adapter->MiniportAdapterListEntry);
            InitializeListHead(&Adapter->MiniportAdapterListEntry);
            if (MpCtx->AdapterCount > 0)
                MpCtx->AdapterCount--;
        }
        KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);
    }

    /* Detach the FDO from the device stack. */
    if (Adapter->LowerDeviceObject != NULL)
    {
        IoDetachDevice(Adapter->LowerDeviceObject);
        Adapter->LowerDeviceObject = NULL;
    }

    /* Ask the miniport to release its per-device context if one was created. */
    if (Adapter->MiniportDeviceContext != NULL && MpCtx != NULL &&
        MpCtx->InitData.s.DxgkDdiRemoveDevice != NULL)
    {
        MpCtx->InitData.s.DxgkDdiRemoveDevice(Adapter->MiniportDeviceContext);
        Adapter->MiniportDeviceContext = NULL;
    }

    /* Delete the FDO.  This also releases the DeviceExtension (Adapter) memory. */
    IoDeleteDevice(DeviceObject);

    /*
     * Return STATUS_NOT_SUPPORTED so the PnP Manager knows this driver
     * cannot handle the device and should try the next driver on the
     * compatible-IDs list (i.e., videoprt.sys for XDDM devices).
     */
    return STATUS_NOT_SUPPORTED;
}
