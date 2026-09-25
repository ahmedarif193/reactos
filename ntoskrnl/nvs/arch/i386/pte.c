#include <nvs/include/mienv.h>
#include "hardware.h"

static BOOLEAN
MiI386PteHas(MI_PTE Pte, MI_PTE Bits)
{
    MI_PTE Required = MI_I386_PTE_PRESENT | Bits;
    return (Pte & Required) == Required;
}

MI_PTE
MiArchPteMakeLeaf(ULONG64 Frame, ULONG Protection, ULONG Flags)
{
    static const MI_PTE Access[8] =
    {
        0, 0, 0, 0,
        MI_I386_PTE_WRITABLE, MI_I386_PTE_COPY,
        MI_I386_PTE_WRITABLE, MI_I386_PTE_COPY
    };
    MI_PTE Attributes;

    if (!MI_PROT_IS_ACCESSIBLE(Protection) || (Protection & MI_PROT_GUARD) || Frame > 0xFFFFF)
        return 0;

    Attributes = Access[Protection & MI_PROT_ACCESS_MASK];
    Attributes |= MI_I386_PTE_PRESENT | MI_I386_PTE_ACCESSED;
    if (Flags & MI_LEAF_USER)
        Attributes |= MI_I386_PTE_USER;
    else if (Flags & MI_LEAF_GLOBAL)
        Attributes |= MI_I386_PTE_GLOBAL;
    if ((Flags & (MI_LEAF_DEVICE | MI_LEAF_NOCACHE)) || (Protection & MI_PROT_NOCACHE))
        Attributes |= MI_I386_PTE_PCD | MI_I386_PTE_PWT;
    else if (Flags & MI_LEAF_WRITECOMBINE)
        Attributes |= MI_I386_PTE_PWT;
    if (Attributes & MI_I386_PTE_WRITABLE)
    {
        if (Flags & (MI_LEAF_DIRTY | MI_LEAF_HARDWARE_DIRTY))
            Attributes |= MI_I386_PTE_WRITE;
        if (Flags & MI_LEAF_DIRTY)
            Attributes |= MI_I386_PTE_DIRTY;
    }

    return ((MI_PTE)(Frame << PAGE_SHIFT) & MI_I386_PTE_FRAME) | Attributes;
}

MI_PTE
MiArchPteMakeTable(ULONG64 Frame, ULONG Flags)
{
    if (Frame > 0xFFFFF)
        return 0;
    return ((MI_PTE)(Frame << PAGE_SHIFT) & MI_I386_PTE_FRAME) |
           MI_I386_PTE_PRESENT | MI_I386_PTE_WRITE | MI_I386_PTE_ACCESSED |
           ((Flags & MI_LEAF_USER) ? MI_I386_PTE_USER : 0);
}

BOOLEAN
MiArchPteIsBlock(MI_PTE Pte, ULONG Level)
{
    return Level == 1 && MiI386PteHas(Pte, MI_I386_PTE_LARGE);
}

MI_PTE
MiArchPteMakeBlock(ULONG64 Frame, ULONG Protection, ULONG Flags)
{
    MI_PTE Leaf = MiArchPteMakeLeaf(Frame, Protection, Flags);
    if ((Frame & 1023) != 0)
        return 0;
    return Leaf ? Leaf | MI_I386_PTE_LARGE : 0;
}

BOOLEAN
MiArchPteIsValid(MI_PTE Pte)
{
    return MiI386PteHas(Pte, 0);
}

ULONG64
MiArchPteFrame(MI_PTE Pte)
{
    return (Pte & MI_I386_PTE_FRAME) >> PAGE_SHIFT;
}

BOOLEAN
MiArchPteIsWritable(MI_PTE Pte)
{
    return MiI386PteHas(Pte, MI_I386_PTE_WRITABLE);
}

BOOLEAN
MiArchPteIsHardwareWritable(MI_PTE Pte)
{
    return MiI386PteHas(Pte, MI_I386_PTE_WRITABLE);
}

BOOLEAN
MiArchPteIsCopyOnWrite(MI_PTE Pte)
{
    return MiI386PteHas(Pte, MI_I386_PTE_COPY);
}

ULONG
MiArchPteLeafFlags(MI_PTE Pte)
{
    if (MiI386PteHas(Pte, MI_I386_PTE_PCD))
        return MI_LEAF_NOCACHE;
    if (MiI386PteHas(Pte, MI_I386_PTE_PWT))
        return MI_LEAF_WRITECOMBINE;
    return 0;
}

BOOLEAN
MiArchPteIsDirty(MI_PTE Pte)
{
    return MiI386PteHas(Pte, MI_I386_PTE_DIRTY);
}

BOOLEAN
MiArchPteIsAccessed(MI_PTE Pte)
{
    return MiI386PteHas(Pte, MI_I386_PTE_ACCESSED);
}

BOOLEAN
MiArchPteIsLeafDescriptor(MI_PTE Pte)
{
    return MiI386PteHas(Pte, 0);
}

BOOLEAN
MiArchIsSelfMapAddress(ULONG64 VirtualAddress)
{
    return VirtualAddress - MI_I386_SELF_BASE < MI_I386_SELF_BYTES;
}

BOOLEAN
MiArchPteIsUser(MI_PTE Pte)
{
    return MiI386PteHas(Pte, MI_I386_PTE_USER);
}

BOOLEAN
MiArchPteIsExecutable(MI_PTE Pte, BOOLEAN UserMode)
{
    UNREFERENCED_PARAMETER(UserMode);
    return MiI386PteHas(Pte, 0);
}

MI_PTE
MiArchPteSetDirty(MI_PTE Pte, BOOLEAN Dirty)
{
    if (!MiI386PteHas(Pte, MI_I386_PTE_WRITABLE))
        return Pte;
    Pte &= ~(MI_I386_PTE_WRITE | MI_I386_PTE_DIRTY);
    return Pte | (Dirty ? MI_I386_PTE_WRITE | MI_I386_PTE_DIRTY : 0);
}

MI_PTE
MiArchPteSetAccessed(MI_PTE Pte, BOOLEAN Accessed)
{
    if (!MiI386PteHas(Pte, 0))
        return Pte;
    return (Pte & ~MI_I386_PTE_ACCESSED) | (Accessed ? MI_I386_PTE_ACCESSED : 0);
}

BOOLEAN
MiArchPteNeedsBreak(MI_PTE Old, MI_PTE New)
{
    UNREFERENCED_PARAMETER(Old);
    UNREFERENCED_PARAMETER(New);
    return FALSE;
}

VOID
MiArchInitializeProcessRoot(ULONG64 SystemRootFrame, ULONG64 ProcessRootFrame)
{
    PMI_PTE System = MiArchMapFrame(SystemRootFrame);
    PMI_PTE Process = MiArchMapFrame(ProcessRootFrame);
    ULONG Index;

    MI_ASSERT(SystemRootFrame != ProcessRootFrame);
    for (Index = 0; Index < 1024; Index++)
    {
        MI_PTE Value = 0;
        if (Index == MI_I386_SELF_INDEX)
            Value = MiArchPteMakeTable(ProcessRootFrame, 0);
        else if (Index >= 512 && Index != MI_I386_HYPER_INDEX)
            Value = MiArchPteRead(&System[Index]);
        MiArchPteWrite(&Process[Index], Value);
    }
    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);
}
