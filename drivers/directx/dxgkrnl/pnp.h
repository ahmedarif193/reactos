/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PnP dispatch declarations and child PDO device extension
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * WDDM types shared across dxgkrnl translation units (DXGK_INTERFACE,
 * DXGK_START_INFO, DXGK_CHILD_DESCRIPTOR, etc.) are defined in
 * dxgkrnl_private.h.  This header declares only the child PDO extension
 * structure and the public PnP/Power entry points.
 */

#ifndef _DXGKRNL_PNP_H_
#define _DXGKRNL_PNP_H_

/*
 * DXGK_CHILD_PDO_EXTENSION
 *
 * Device extension for a child PDO created by dxgkrnl (one per child
 * device reported by DxgkDdiQueryChildRelations).  Mirrors the pattern
 * used by videoprt's VIDEO_PORT_CHILD_EXTENSION.
 *
 * The Signature field is the first ULONG; DxgkpMiniportPnpDispatch reads
 * it to distinguish child PDOs from the GPU FDO.
 */
typedef struct _DXGK_CHILD_PDO_EXTENSION
{
    /* Signature / type tag — must be first field */
    ULONG                   Signature;  /* DXGK_CHILD_PDO_SIGNATURE */

    /* Back-pointer to the parent GPU FDO adapter context */
    PDXGKRNL_ADAPTER        ParentAdapter;

    /* The PDO device object itself */
    PDEVICE_OBJECT          DeviceObject;

    /* Descriptor as returned by DxgkDdiQueryChildRelations */
    DXGK_CHILD_DESCRIPTOR   Descriptor;

    /* Link into ParentAdapter->ChildListHead (guarded by ChildListLock) */
    LIST_ENTRY              ListEntry;

    /* TRUE while this child is still reported by the miniport */
    BOOLEAN                 Present;

    /* Cached connector status (hot-plug aware children only) */
    BOOLEAN                 Connected;

    /* Cached EDID blob (if obtained from DxgkDdiQueryDeviceDescriptor) */
    UCHAR                   Edid[128];
    BOOLEAN                 EdidValid;

} DXGK_CHILD_PDO_EXTENSION, *PDXGK_CHILD_PDO_EXTENSION;

/* Pool tag for child PDO extensions */
#define TAG_DXGK_CHILD_PDO  'CxgD'

/*
 * Signature stored in DXGK_CHILD_PDO_EXTENSION::Signature.
 * DxgkpMiniportPnpDispatch compares *(PULONG)DevExt against this to
 * determine whether a device object is a child PDO or a GPU FDO.
 * The GPU FDO's first field is a PDXGKRNL_MINIPORT_CONTEXT pointer,
 * which will never equal this constant.
 */
#define DXGK_CHILD_PDO_SIGNATURE  0x43786744UL  /* 'DgxC' */

/* -------------------------------------------------------------------------
 * PnP / Power dispatch entry points
 *
 * DxgkpMiniportPnpDispatch   — installed as IRP_MJ_PNP handler for both
 *                              the GPU FDO and child PDOs created by dxgkrnl.
 * DxgkpMiniportPowerDispatch — installed as IRP_MJ_POWER handler.
 *
 * Both are registered by DxgkInitialize (in adapter.c) after the miniport's
 * own dispatch table has been replaced.
 * ------------------------------------------------------------------------- */

DRIVER_DISPATCH DxgkpMiniportPnpDispatch;
DRIVER_DISPATCH DxgkpMiniportPowerDispatch;

/* -------------------------------------------------------------------------
 * Internal PnP helpers (defined in pnp.c, used within dxgkrnl only)
 * ------------------------------------------------------------------------- */

/*
 * DxgkpQueryBusRelations
 *   Build DEVICE_RELATIONS for BusRelations by calling
 *   DxgkDdiQueryChildRelations on the miniport, creating child PDOs
 *   as needed.
 */
NTSTATUS
DxgkpQueryBusRelations(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ PDEVICE_RELATIONS *Relations);

/*
 * DxgkpCreateChildPdo
 *   Allocate and initialise a PDO for one child device.
 */
NTSTATUS
DxgkpCreateChildPdo(
    _In_  PDXGKRNL_ADAPTER          Adapter,
    _In_  PDXGK_CHILD_DESCRIPTOR    Descriptor,
    _Out_ PDXGK_CHILD_PDO_EXTENSION *ChildExtension);

/*
 * DxgkpDeleteChildPdo
 *   Detach and free a previously created child PDO.
 */
VOID
DxgkpDeleteChildPdo(
    _In_ PDXGK_CHILD_PDO_EXTENSION ChildExtension);

/*
 * DxgkpChildPdoPnpDispatch
 *   IRP_MJ_PNP handler for child PDOs.  Called from
 *   DxgkpMiniportPnpDispatch after signature-based dispatch.
 */
NTSTATUS
DxgkpChildPdoPnpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

/*
 * DxgkpBuildDeviceInfo
 *   Populate a DXGK_DEVICE_INFO from the adapter's PCI resources and the
 *   miniport registry path.  Called by DxgkCbGetDeviceInformation (adapter.c).
 */
VOID
DxgkpBuildDeviceInfo(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ PDXGK_DEVICE_INFO DeviceInfo);

#endif /* _DXGKRNL_PNP_H_ */
