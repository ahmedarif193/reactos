/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ARM64 hypervisor vendor identification
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/drivers/acpi/acpi.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

static CHAR HvlHypervisorVendorId[sizeof(ULONGLONG) + 1];
static volatile LONG HvlHypervisorVendorState;

/* PUBLIC FUNCTIONS **********************************************************/

PCSTR
NTAPI
HvlGetHypervisorVendorId(VOID)
{
    PFADT Fadt;
    LONG State;
    ULONGLONG VendorId;

    /*
     * ACPI 6.0 defines the FADT Hypervisor Vendor Identity as the ARM64
     * platform source of truth.  The HAL cache owns the table for the life of
     * the system, while this local copy gives callers a stable NUL-terminated
     * string.  A zero identity means that the firmware advertised no vendor.
     */
    State = ReadAcquire(&HvlHypervisorVendorState);
    if (State != 2)
    {
        if (State == 0)
        {
            Fadt = HalGetCachedAcpiTable(FADT_SIGNATURE, NULL, NULL);
            if ((Fadt == NULL) ||
                (Fadt->Header.Length <
                    RTL_SIZEOF_THROUGH_FIELD(FADT, hypervisor_id)))
            {
                return NULL;
            }

            VendorId = Fadt->hypervisor_id;
            if (VendorId == 0)
                return NULL;

            if (InterlockedCompareExchange(&HvlHypervisorVendorState,
                                           1,
                                           0) == 0)
            {
                RtlCopyMemory(HvlHypervisorVendorId,
                              &VendorId,
                              sizeof(VendorId));
                HvlHypervisorVendorId[sizeof(VendorId)] = ANSI_NULL;
                WriteRelease(&HvlHypervisorVendorState, 2);
                State = 2;
            }
        }

        while ((State = ReadAcquire(&HvlHypervisorVendorState)) == 1)
            YieldProcessor();
        if (State != 2)
            return NULL;
    }

    /* Windows treats the Qualcomm platform identity as a non-hypervisor. */
    if (RtlCompareMemory(HvlHypervisorVendorId, "QCOM", 4) == 4)
        return NULL;

    return HvlHypervisorVendorId;
}
