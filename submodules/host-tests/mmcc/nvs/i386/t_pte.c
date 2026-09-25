#include <nvs/include/mienv.h>
#include <nvs/arch/i386/hardware.h>

static ULONG Checks, Failures;
static MI_PTE Roots[2][1024];

#define CHECK(e) do { Checks++; if (!(e)) { if (Failures++ < 20) fprintf(stderr, "%u: %s\n", __LINE__, #e); } } while (0)

PVOID MiArchMapFrame(ULONG64 Frame) { MI_ASSERT(Frame < 2); return Roots[Frame]; }
VOID MiArchUnmapFrame(PVOID Mapping) { UNREFERENCED_PARAMETER(Mapping); }
MI_PTE MiArchPteRead(PMI_PTE Slot) { return *Slot; }
VOID MiArchPteWrite(PMI_PTE Slot, MI_PTE Value) { *Slot = Value; }

int main(void)
{
    ULONG Protection, Flags, Index;
    MI_PTE Pte, Updated;
    const ULONG64 Frame = 0xABCDE;

    CHECK(sizeof(MI_PTE) == 4);
    for (Protection = 0; Protection <= MI_PROT_MASK; Protection++)
    {
        for (Flags = 0; Flags < 128; Flags++)
        {
            Pte = MiArchPteMakeLeaf(Frame, Protection, Flags);
            if (!MI_PROT_IS_ACCESSIBLE(Protection) || (Protection & MI_PROT_GUARD))
            {
                CHECK(Pte == 0);
                continue;
            }
            CHECK(MiArchPteIsValid(Pte));
            CHECK(MiArchPteFrame(Pte) == Frame);
            CHECK(MiArchPteIsUser(Pte) == !!(Flags & MI_LEAF_USER));
            CHECK(MiArchPteIsExecutable(Pte, TRUE));
            CHECK(MiArchPteIsWritable(Pte) == !!MI_PROT_IS_WRITABLE(Protection));
            CHECK(MiArchPteIsCopyOnWrite(Pte) == !!MI_PROT_IS_COPY(Protection));
            CHECK(!!(Pte & MI_I386_PTE_GLOBAL) == !!((Flags & MI_LEAF_GLOBAL) && !(Flags & MI_LEAF_USER)));
            CHECK(!!(Pte & MI_I386_PTE_WRITE) == !!(MI_PROT_IS_WRITABLE(Protection) &&
                                                     (Flags & (MI_LEAF_DIRTY | MI_LEAF_HARDWARE_DIRTY))));
            CHECK(MiArchPteIsDirty(Pte) == !!(MI_PROT_IS_WRITABLE(Protection) && (Flags & MI_LEAF_DIRTY)));
            Updated = MiArchPteSetDirty(Pte, TRUE);
            CHECK(MiArchPteFrame(Updated) == Frame);
            CHECK(MiArchPteIsDirty(Updated) == !!MI_PROT_IS_WRITABLE(Protection));
            Updated = MiArchPteSetDirty(Updated, FALSE);
            CHECK((Updated & (MI_I386_PTE_WRITE | MI_I386_PTE_DIRTY)) == 0);
            CHECK(!MiArchPteIsAccessed(MiArchPteSetAccessed(Pte, FALSE)));
            CHECK(MiArchPteIsAccessed(MiArchPteSetAccessed(Pte, TRUE)));
        }
    }

    CHECK(MiArchPteMakeLeaf(0x100000, MI_PROT_READWRITE, 0) == 0);
    CHECK(MiArchPteMakeTable(0x100000, 0) == 0);
    CHECK(MiArchPteMakeBlock(1, MI_PROT_READWRITE, 0) == 0);
    Pte = MiArchPteMakeBlock(0x400, MI_PROT_READWRITE, MI_LEAF_DIRTY);
    CHECK(MiArchPteIsBlock(Pte, 1));
    CHECK(!MiArchPteIsBlock(Pte, 0));
    CHECK(MiArchIsSelfMapAddress(MI_I386_SELF_BASE));
    CHECK(MiArchIsSelfMapAddress(MI_I386_SELF_BASE + MI_I386_SELF_BYTES - 1));
    CHECK(!MiArchIsSelfMapAddress(MI_I386_SELF_BASE - 1));
    CHECK(!MiArchIsSelfMapAddress(MI_I386_SELF_BASE + MI_I386_SELF_BYTES));

    for (Index = 0; Index < 1024; Index++)
    {
        Roots[0][Index] = MiArchPteMakeTable(Index + 16, 0);
        Roots[1][Index] = ~(MI_PTE)0;
    }
    MiArchInitializeProcessRoot(0, 1);
    for (Index = 0; Index < 1024; Index++)
    {
        if (Index < 512 || Index == MI_I386_HYPER_INDEX)
            CHECK(Roots[1][Index] == 0);
        else if (Index == MI_I386_SELF_INDEX)
            CHECK(Roots[1][Index] == MiArchPteMakeTable(1, 0));
        else
            CHECK(Roots[1][Index] == Roots[0][Index]);
    }
    printf("i386 PTE: %u checks, %u failures\n", Checks, Failures);
    return Failures != 0;
}
