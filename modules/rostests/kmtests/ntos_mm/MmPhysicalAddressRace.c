/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <kmt_test.h>

typedef struct _TRANSLATION_RACE
{
    PVOID InterruptAddress;
    volatile LONG InterruptCount;
    volatile LONGLONG InterruptPhysical;
} TRANSLATION_RACE;

static VOID NTAPI
TranslationDpc(PKDPC Dpc, PVOID Context, PVOID Argument1, PVOID Argument2)
{
    TRANSLATION_RACE *Race = Context;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    Race->InterruptPhysical = MmGetPhysicalAddress(Race->InterruptAddress).QuadPart;
    InterlockedIncrement(&Race->InterruptCount);
}

START_TEST(MmPhysicalAddressRace)
{
    PHYSICAL_ADDRESS Low = {{0}}, High, Skip = {{0}};
    PMDL Mdl;
    PUCHAR Address;
    TRANSLATION_RACE Race;
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER Due;
    KAFFINITY OldAffinity;
    ULONGLONG Start, Samples = 0, Failures = 0;
    LONGLONG FirstExpected = 0, FirstActual = 0;
    ULONG Offset;

    High.QuadPart = MAXLONGLONG;
    Mdl = MmAllocatePagesForMdl(Low, High, Skip, 2 * PAGE_SIZE);
    ok(Mdl != NULL, "Failed to allocate test pages\n");
    if (!Mdl) return;
    if (MmGetMdlByteCount(Mdl) < 2 * PAGE_SIZE)
    {
        ok(FALSE, "Insufficient test pages\n");
        goto FreePages;
    }
    Address = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    ok(Address != NULL, "Failed to map test pages\n");
    if (!Address) goto FreePages;

    RtlZeroMemory(&Race, sizeof(Race));
    Race.InterruptAddress = Address + PAGE_SIZE;
    OldAffinity = KeSetSystemAffinityThreadEx(1);
    KeInitializeDpc(&Dpc, TranslationDpc, &Race);
    KeSetTargetProcessorDpc(&Dpc, 0);
    KeInitializeTimer(&Timer);
    Due.QuadPart = -10000;
    KeSetTimerEx(&Timer, Due, 1, &Dpc);
    Start = KeQueryInterruptTime();
    do
    {
        for (Offset = 0; Offset < PAGE_SIZE; Offset += 8)
        {
            LONGLONG Expected = ((ULONGLONG)MmGetMdlPfnArray(Mdl)[0] << PAGE_SHIFT) + Offset;
            LONGLONG Actual = MmGetPhysicalAddress(Address + Offset).QuadPart;
            if (Actual != Expected)
            {
                if (!Failures) { FirstExpected = Expected; FirstActual = Actual; }
                ++Failures;
            }
            ++Samples;
        }
    } while (KeQueryInterruptTime() - Start < 100000000ULL);
    KeCancelTimer(&Timer);
    KeRemoveQueueDpc(&Dpc);
    KeRevertToUserAffinityThreadEx(OldAffinity);
    KeFlushQueuedDpcs();

    ok(Race.InterruptCount >= 10, "Only %ld interfering DPCs ran\n", Race.InterruptCount);
    ok(Failures == 0, "%I64u wrong physical addresses in %I64u translations\n", Failures, Samples);
    ok((ULONGLONG)Race.InterruptPhysical == ((ULONGLONG)MmGetMdlPfnArray(Mdl)[1] << PAGE_SHIFT),
       "DPC physical address mismatch: %I64x\n", Race.InterruptPhysical);
    MmUnmapLockedPages(Address, Mdl);
FreePages:
    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}
