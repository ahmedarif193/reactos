#include <nvs/nt/mint.h>
#include "hardware.h"

static BOOLEAN MiI386DirectMapReady;

VOID
MiI386SetDirectMapReady(VOID)
{
    MiI386DirectMapReady = TRUE;
}

PVOID
MiArchMapFrame(ULONG64 Frame)
{
    PMI_PTE Root = (PMI_PTE)(ULONG_PTR)MI_I386_ROOT_VA;
    ULONG Address, Index, Slot;
    MI_PTE Pde;

    ASSERT(Frame < MI_I386_DIRECT_PAGES);
    if (Frame >= MI_I386_DIRECT_PAGES)
        return NULL;

    Address = MI_I386_DIRECT_BASE + (ULONG)(Frame << PAGE_SHIFT);
    if (MiI386DirectMapReady)
        return (PVOID)(ULONG_PTR)Address;

    Index = Address >> 22;
    Slot = (Address >> PAGE_SHIFT) & 1023;
    Pde = MiArchPteRead(&Root[Index]);
    if (MiArchPteIsValid(Pde))
    {
        if (MiArchPteIsBlock(Pde, 1))
        {
            if ((MiArchPteFrame(Pde) & ~1023ULL) + Slot == Frame)
                return (PVOID)(ULONG_PTR)Address;
        }
        else
        {
            PMI_PTE Table = (PMI_PTE)(ULONG_PTR)(MI_I386_SELF_BASE + (Index << PAGE_SHIFT));
            MI_PTE Pte = MiArchPteRead(&Table[Slot]);
            if (MiArchPteIsValid(Pte) && MiArchPteFrame(Pte) == Frame)
                return (PVOID)(ULONG_PTR)Address;
        }
    }

    if (Frame == MiArchBootRootFrame())
        return (PVOID)(ULONG_PTR)MI_I386_ROOT_VA;

    for (Index = 0; Index < 1024; Index++)
    {
        Pde = MiArchPteRead(&Root[Index]);
        if (MiArchPteIsValid(Pde) && !MiArchPteIsBlock(Pde, 1) && MiArchPteFrame(Pde) == Frame)
            return (PVOID)(ULONG_PTR)(MI_I386_SELF_BASE + (Index << PAGE_SHIFT));
    }
    return NULL;
}

VOID
MiArchUnmapFrame(PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

MI_PTE
MiArchPteRead(PMI_PTE Slot)
{
    return __atomic_load_n(Slot, __ATOMIC_ACQUIRE);
}

VOID
MiArchPteWrite(PMI_PTE Slot, MI_PTE Value)
{
    __atomic_store_n(Slot, Value, __ATOMIC_RELEASE);
}

BOOLEAN
MiArchPteCompareExchange(PMI_PTE Slot, MI_PTE Expected, MI_PTE Value)
{
    return (BOOLEAN)__atomic_compare_exchange_n(Slot, &Expected, Value, FALSE,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

ULONG64
MiArchBootRootFrame(VOID)
{
    return (__readcr3() & MI_I386_PTE_FRAME) >> PAGE_SHIFT;
}

VOID
MiArchBootClearUserHalf(ULONG64 RootFrame)
{
    PMI_PTE Root = MiArchMapFrame(RootFrame);
    ULONG Index;

    for (Index = 0; Index < 512; Index++)
        MiArchPteWrite(&Root[Index], 0);
    MiArchTlbInvalidateAll(TRUE);
}
