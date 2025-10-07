/*
 * PROJECT:         ReactOS ACPI Subsystem
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * FILE:            drivers/bus/acpi/acpica/utilities/rosmem.c
 * PURPOSE:         ACPICA memory wrappers to catch suspect overwrites
 * COPYRIGHT:       Copyright (c) Ahmed ARIF (arif.ing@outlook.com)
 */

/* FIXME: temporary instrumentation – remove once the offending caller is fixed. */

#define ACPI_ROS_MEMWRAP_DISABLED 1
#include "acpi.h"
#include "accommon.h"

/* ReactOS debug prints */
#include <debug.h>

void *AcpiRos_SuspectNode = NULL;
size_t AcpiRos_SuspectNodeSize = 0;

static __inline BOOLEAN Overlap(const void *dst, size_t n)
{
    const char *d = (const char *)dst;
    const char *s = (const char *)AcpiRos_SuspectNode;
    size_t sz = AcpiRos_SuspectNodeSize;
    if (!s || sz == 0) return FALSE;
    const char *dend = d + n;
    const char *send = s + sz;
    return (d < send) && (dend > s);
}

void *AcpiRosMemcpy(void *dst, const void *src, size_t n, void *retaddr)
{
    static volatile LONG once = 0;
    if (Overlap(dst, n) && InterlockedCompareExchange(&once, 1, 0) == 0)
    {
        void *ra1 = __builtin_return_address(1);
        void *ra2 = __builtin_return_address(2);
        DPRINT1("WRAP memcpy overlap suspect: dst=%p..%p n=%zu caller=%p ra1=%p ra2=%p suspect=%p..%p\n",
                dst, (const char*)dst + n, n, retaddr, ra1, ra2,
                AcpiRos_SuspectNode, (const char*)AcpiRos_SuspectNode + AcpiRos_SuspectNodeSize);
    }
    RtlCopyMemory(dst, src, n);
    return dst;
}

void *AcpiRosMemmove(void *dst, const void *src, size_t n, void *retaddr)
{
    static volatile LONG once = 0;
    if (Overlap(dst, n) && InterlockedCompareExchange(&once, 1, 0) == 0)
    {
        void *ra1 = __builtin_return_address(1);
        void *ra2 = __builtin_return_address(2);
        DPRINT1("WRAP memmove overlap suspect: dst=%p..%p n=%zu caller=%p ra1=%p ra2=%p suspect=%p..%p\n",
                dst, (const char*)dst + n, n, retaddr, ra1, ra2,
                AcpiRos_SuspectNode, (const char*)AcpiRos_SuspectNode + AcpiRos_SuspectNodeSize);
    }
    RtlMoveMemory(dst, src, n);
    return dst;
}

void *AcpiRosMemset(void *dst, int c, size_t n, void *retaddr)
{
    static volatile LONG once = 0;
    if (Overlap(dst, n) && InterlockedCompareExchange(&once, 1, 0) == 0)
    {
        void *ra1 = __builtin_return_address(1);
        void *ra2 = __builtin_return_address(2);
        DPRINT1("WRAP memset overlap suspect: dst=%p..%p n=%zu val=0x%02X caller=%p ra1=%p ra2=%p suspect=%p..%p\n",
                dst, (const char*)dst + n, n, (unsigned)(c & 0xFF), retaddr, ra1, ra2,
                AcpiRos_SuspectNode, (const char*)AcpiRos_SuspectNode + AcpiRos_SuspectNodeSize);
    }
    RtlFillMemory(dst, n, (UCHAR)c);
    return dst;
}
