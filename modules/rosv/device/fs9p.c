/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Filesystem mount table and network port forwarding table
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 *
 * This module manages:
 *   - Host-to-guest filesystem mount point mappings (9P-style)
 *   - Network port forwarding table entries
 *
 * The actual 9P protocol and packet forwarding are follow-up work.
 * For now, we track the configuration tables and provide IOCTL interfaces.
 */

#include <rosv/rosv.h>
#include <rosv/pty.h>

/* ---- Filesystem mount table --------------------------------------------- */

static ROSV_FS_MOUNT_INFO g_MountTable[ROSV_MOUNT_MAX];
static KSPIN_LOCK g_MountTableLock;
static BOOLEAN g_MountTableInitialized = FALSE;

static VOID
RosvFsMountTableInit(VOID)
{
    if (!g_MountTableInitialized)
    {
        KeInitializeSpinLock(&g_MountTableLock);
        RtlZeroMemory(g_MountTable, sizeof(g_MountTable));
        g_MountTableInitialized = TRUE;
        ROSV_TRACE("FS mount table initialized, max=%d", ROSV_MOUNT_MAX);
    }
}

/* ---- Mount Add ---------------------------------------------------------- */

NTSTATUS
RosvFsMountAdd(
    _In_ PROSV_FS_MOUNT_REQUEST Request)
{
    ULONG i;
    KIRQL OldIrql;

    if (Request == NULL)
    {
        ROSV_ERR("RosvFsMountAdd: Request is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    RosvFsMountTableInit();

    ROSV_TRACE("RosvFsMountAdd: HostPath=\"%S\" GuestMount=\"%s\" Flags=0x%X",
               Request->HostPath, Request->GuestMountPoint, Request->Flags);

    /* Validate inputs */
    if (Request->HostPath[0] == L'\0')
    {
        ROSV_ERR("RosvFsMountAdd: HostPath is empty");
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->GuestMountPoint[0] == '\0')
    {
        ROSV_ERR("RosvFsMountAdd: GuestMountPoint is empty");
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&g_MountTableLock, &OldIrql);

    /* Check for duplicate guest mount point */
    for (i = 0; i < ROSV_MOUNT_MAX; i++)
    {
        if (g_MountTable[i].Active)
        {
            if (strncmp(g_MountTable[i].GuestMountPoint,
                        Request->GuestMountPoint,
                        ROSV_MOUNT_PATH_MAX) == 0)
            {
                KeReleaseSpinLock(&g_MountTableLock, OldIrql);
                ROSV_ERR("RosvFsMountAdd: Duplicate guest mount point \"%s\"",
                         Request->GuestMountPoint);
                return STATUS_OBJECT_NAME_COLLISION;
            }
        }
    }

    /* Find a free slot */
    for (i = 0; i < ROSV_MOUNT_MAX; i++)
    {
        if (!g_MountTable[i].Active)
        {
            RtlCopyMemory(g_MountTable[i].HostPath,
                          Request->HostPath,
                          sizeof(Request->HostPath));
            RtlCopyMemory(g_MountTable[i].GuestMountPoint,
                          Request->GuestMountPoint,
                          sizeof(Request->GuestMountPoint));
            g_MountTable[i].Flags = Request->Flags;
            g_MountTable[i].Active = TRUE;

            KeReleaseSpinLock(&g_MountTableLock, OldIrql);

            ROSV_TRACE("RosvFsMountAdd: Mount added at slot %u: \"%S\" -> \"%s\" flags=0x%X",
                       i, Request->HostPath, Request->GuestMountPoint, Request->Flags);
            return STATUS_SUCCESS;
        }
    }

    KeReleaseSpinLock(&g_MountTableLock, OldIrql);
    ROSV_ERR("RosvFsMountAdd: No free mount slots (max=%d)", ROSV_MOUNT_MAX);
    return STATUS_QUOTA_EXCEEDED;
}

/* ---- Mount Remove ------------------------------------------------------- */

NTSTATUS
RosvFsMountRemove(
    _In_ PROSV_FS_MOUNT_REQUEST Request)
{
    ULONG i;
    KIRQL OldIrql;

    if (Request == NULL)
    {
        ROSV_ERR("RosvFsMountRemove: Request is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    RosvFsMountTableInit();

    ROSV_TRACE("RosvFsMountRemove: GuestMount=\"%s\"", Request->GuestMountPoint);

    if (Request->GuestMountPoint[0] == '\0')
    {
        ROSV_ERR("RosvFsMountRemove: GuestMountPoint is empty");
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&g_MountTableLock, &OldIrql);

    for (i = 0; i < ROSV_MOUNT_MAX; i++)
    {
        if (g_MountTable[i].Active &&
            strncmp(g_MountTable[i].GuestMountPoint,
                    Request->GuestMountPoint,
                    ROSV_MOUNT_PATH_MAX) == 0)
        {
            ROSV_TRACE("RosvFsMountRemove: Removing mount at slot %u: \"%S\" -> \"%s\"",
                       i, g_MountTable[i].HostPath, g_MountTable[i].GuestMountPoint);
            RtlZeroMemory(&g_MountTable[i], sizeof(ROSV_FS_MOUNT_INFO));
            KeReleaseSpinLock(&g_MountTableLock, OldIrql);
            return STATUS_SUCCESS;
        }
    }

    KeReleaseSpinLock(&g_MountTableLock, OldIrql);
    ROSV_ERR("RosvFsMountRemove: Mount point \"%s\" not found", Request->GuestMountPoint);
    return STATUS_NOT_FOUND;
}

/* ---- Mount List --------------------------------------------------------- */

NTSTATUS
RosvFsMountList(
    _Out_ PROSV_FS_MOUNT_LIST Result,
    _In_ ULONG MaxOutputSize,
    _Out_ PULONG OutputBytes)
{
    ULONG i;
    ULONG Count = 0;
    KIRQL OldIrql;

    if (Result == NULL || OutputBytes == NULL)
    {
        ROSV_ERR("RosvFsMountList: invalid parameters (Result=%p OutputBytes=%p)",
                 Result, OutputBytes);
        return STATUS_INVALID_PARAMETER;
    }

    RosvFsMountTableInit();

    ROSV_TRACE("RosvFsMountList: MaxOutputSize=%u", MaxOutputSize);

    if (MaxOutputSize < sizeof(ROSV_FS_MOUNT_LIST))
    {
        ROSV_ERR("RosvFsMountList: Output buffer too small (%u < %u)",
                 MaxOutputSize, (ULONG)sizeof(ROSV_FS_MOUNT_LIST));
        *OutputBytes = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    KeAcquireSpinLock(&g_MountTableLock, &OldIrql);

    for (i = 0; i < ROSV_MOUNT_MAX; i++)
    {
        if (g_MountTable[i].Active)
        {
            Result->Mounts[Count] = g_MountTable[i];
            Count++;
        }
    }

    KeReleaseSpinLock(&g_MountTableLock, OldIrql);

    /* Zero out unused entries */
    for (i = Count; i < ROSV_MOUNT_MAX; i++)
    {
        RtlZeroMemory(&Result->Mounts[i], sizeof(ROSV_FS_MOUNT_INFO));
    }

    Result->Count = Count;
    *OutputBytes = sizeof(ROSV_FS_MOUNT_LIST);

    ROSV_TRACE("RosvFsMountList: Returning %u mounts", Count);

    return STATUS_SUCCESS;
}

/* ---- Network port forwarding table -------------------------------------- */

static ROSV_NET_FORWARD_REQUEST g_PortForwards[ROSV_PORT_FORWARD_MAX];
static BOOLEAN g_PortForwardActive[ROSV_PORT_FORWARD_MAX];
static KSPIN_LOCK g_PortForwardLock;
static BOOLEAN g_PortForwardInitialized = FALSE;

static VOID
RosvNetForwardTableInit(VOID)
{
    if (!g_PortForwardInitialized)
    {
        KeInitializeSpinLock(&g_PortForwardLock);
        RtlZeroMemory(g_PortForwards, sizeof(g_PortForwards));
        RtlZeroMemory(g_PortForwardActive, sizeof(g_PortForwardActive));
        g_PortForwardInitialized = TRUE;
        ROSV_TRACE("Port forward table initialized, max=%d", ROSV_PORT_FORWARD_MAX);
    }
}

/* ---- Net Forward -------------------------------------------------------- */

NTSTATUS
RosvNetForwardAdd(
    _In_ PROSV_NET_FORWARD_REQUEST Request)
{
    ULONG i;
    KIRQL OldIrql;

    if (Request == NULL)
    {
        ROSV_ERR("RosvNetForwardAdd: Request is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    RosvNetForwardTableInit();

    ROSV_TRACE("RosvNetForwardAdd: HostPort=%u GuestPort=%u Protocol=%u",
               Request->HostPort, Request->GuestPort, Request->Protocol);

    /* Validate protocol */
    if (Request->Protocol != 6 && Request->Protocol != 17)
    {
        ROSV_ERR("RosvNetForwardAdd: Invalid protocol %u (must be 6=TCP or 17=UDP)",
                 Request->Protocol);
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->HostPort == 0 || Request->GuestPort == 0)
    {
        ROSV_ERR("RosvNetForwardAdd: Port cannot be 0 (host=%u guest=%u)",
                 Request->HostPort, Request->GuestPort);
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&g_PortForwardLock, &OldIrql);

    /* Check for duplicate */
    for (i = 0; i < ROSV_PORT_FORWARD_MAX; i++)
    {
        if (g_PortForwardActive[i] &&
            g_PortForwards[i].HostPort == Request->HostPort &&
            g_PortForwards[i].Protocol == Request->Protocol)
        {
            KeReleaseSpinLock(&g_PortForwardLock, OldIrql);
            ROSV_ERR("RosvNetForwardAdd: Host port %u/%s already forwarded",
                     Request->HostPort,
                     Request->Protocol == 6 ? "TCP" : "UDP");
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }

    /* Find a free slot */
    for (i = 0; i < ROSV_PORT_FORWARD_MAX; i++)
    {
        if (!g_PortForwardActive[i])
        {
            g_PortForwards[i] = *Request;
            g_PortForwardActive[i] = TRUE;

            KeReleaseSpinLock(&g_PortForwardLock, OldIrql);

            ROSV_TRACE("RosvNetForwardAdd: Forward added at slot %u: "
                       "host:%u -> guest:%u (%s)",
                       i, Request->HostPort, Request->GuestPort,
                       Request->Protocol == 6 ? "TCP" : "UDP");
            return STATUS_SUCCESS;
        }
    }

    KeReleaseSpinLock(&g_PortForwardLock, OldIrql);
    ROSV_ERR("RosvNetForwardAdd: No free forwarding slots (max=%d)", ROSV_PORT_FORWARD_MAX);
    return STATUS_QUOTA_EXCEEDED;
}

/* ---- Net Unforward ------------------------------------------------------ */

NTSTATUS
RosvNetForwardRemove(
    _In_ PROSV_NET_FORWARD_REQUEST Request)
{
    ULONG i;
    KIRQL OldIrql;

    if (Request == NULL)
    {
        ROSV_ERR("RosvNetForwardRemove: Request is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    RosvNetForwardTableInit();

    if (Request->Protocol != 6 && Request->Protocol != 17)
    {
        ROSV_ERR("RosvNetForwardRemove: Invalid protocol %u (must be 6=TCP or 17=UDP)",
                 Request->Protocol);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvNetForwardRemove: HostPort=%u Protocol=%u",
               Request->HostPort, Request->Protocol);

    KeAcquireSpinLock(&g_PortForwardLock, &OldIrql);

    for (i = 0; i < ROSV_PORT_FORWARD_MAX; i++)
    {
        if (g_PortForwardActive[i] &&
            g_PortForwards[i].HostPort == Request->HostPort &&
            g_PortForwards[i].Protocol == Request->Protocol)
        {
            ROSV_TRACE("RosvNetForwardRemove: Removing forward at slot %u: "
                       "host:%u -> guest:%u (%s)",
                       i, g_PortForwards[i].HostPort, g_PortForwards[i].GuestPort,
                       g_PortForwards[i].Protocol == 6 ? "TCP" : "UDP");

            RtlZeroMemory(&g_PortForwards[i], sizeof(ROSV_NET_FORWARD_REQUEST));
            g_PortForwardActive[i] = FALSE;

            KeReleaseSpinLock(&g_PortForwardLock, OldIrql);
            return STATUS_SUCCESS;
        }
    }

    KeReleaseSpinLock(&g_PortForwardLock, OldIrql);
    ROSV_ERR("RosvNetForwardRemove: Forward for host port %u/%s not found",
             Request->HostPort,
             Request->Protocol == 6 ? "TCP" : "UDP");
    return STATUS_NOT_FOUND;
}
