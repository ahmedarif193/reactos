/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         ARM64 user-buffer probe hooks
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
MiArm64ProbeForWrite(
    _In_ ULONG_PTR Current,
    _In_ ULONG_PTR Last)
{
    ULONG_PTR Page;
    ULONG_PTR EndPage;

    /*
     * PSEH2 on ARM64 cannot reliably catch hardware page faults from the legacy
     * `*p = *p` probe loop. Instead, walk each covered page explicitly and use
     * MmAccessFault to:
     * - page in not-present user pages,
     * - promote clean writable pages to dirty/writable,
     * - break COW pages on first write,
     * - raise the underlying NTSTATUS before callers take more state.
     */
    EndPage = PAGE_ROUND_DOWN(Last);
    Page = Current;

    for (;;)
    {
        ULONG Attempt;

        for (Attempt = 0; Attempt < 4; Attempt++)
        {
            PMMPTE PointerPte;
            NTSTATUS Status;

            PointerPte = MiArm64UserPteKseg0((PVOID)Page);
            if ((PointerPte != NULL) && PointerPte->u.Hard.Valid)
            {
                if (MI_IS_PAGE_WRITEABLE(PointerPte) &&
                    MI_IS_PAGE_DIRTY(PointerPte))
                {
                    break;
                }

                Status = MmAccessFaultEx(0x3, (PVOID)Page, KernelMode, NULL, FALSE);
            }
            else
            {
                Status = MmAccessFaultEx(0x2, (PVOID)Page, KernelMode, NULL, FALSE);
            }

            if (!NT_SUCCESS(Status))
            {
                ExRaiseStatus(Status);
            }
        }

        if (Attempt == 4)
        {
            ExRaiseAccessViolation();
        }

        if (PAGE_ROUND_DOWN(Page) == EndPage)
        {
            break;
        }

        Page = PAGE_ROUND_DOWN(Page) + PAGE_SIZE;
    }
}
