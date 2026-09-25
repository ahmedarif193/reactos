#include <nvs/nt/mint.h>

static VOID
MiI386FlushLocalAll(VOID)
{
    ULONG Cr4 = __readcr4();

    if (Cr4 & CR4_PGE)
    {
        __writecr4(Cr4 & ~CR4_PGE);
        __writecr3(__readcr3());
        __writecr4(Cr4);
    }
    else
    {
        __writecr3(__readcr3());
    }
}

VOID
MiArchInvalidateTlbSingle(PVOID VirtualAddress, MI_TLB_SCOPE Scope)
{
    ASSERT(Scope == MiTlbLocal || KeNumberProcessors == 1);
    __invlpg(VirtualAddress);
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG_PTR Address = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ULONG_PTR End;

    ASSERT(Scope == MiTlbLocal || KeNumberProcessors == 1);
    if (Size == 0)
        return;
    if (Size > (SIZE_T)-1 - (ULONG_PTR)BaseAddress || Size > 256 * PAGE_SIZE)
    {
        MiArchInvalidateTlbAll(Scope);
        return;
    }
    End = ((ULONG_PTR)BaseAddress + Size - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);
    for (;;)
    {
        __invlpg((PVOID)Address);
        if (Address == End)
            break;
        Address += PAGE_SIZE;
    }
}

VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    ASSERT(Scope == MiTlbLocal || KeNumberProcessors == 1);
    MiI386FlushLocalAll();
}

VOID
MiArchTlbInvalidate(ULONG64 VirtualAddress, ULONG64 PageCount, BOOLEAN AllProcessors)
{
    MI_TLB_SCOPE Scope = AllProcessors ? MiTlbAllProcessors : MiTlbLocal;

    if (PageCount > ((SIZE_T)-1 >> PAGE_SHIFT))
        MiArchInvalidateTlbAll(Scope);
    else
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)VirtualAddress,
                                 (SIZE_T)(PageCount << PAGE_SHIFT), Scope);
}

VOID
MiArchTlbInvalidateAll(BOOLEAN AllProcessors)
{
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
}
