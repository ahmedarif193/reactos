/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/amd64/t_pte.c
 * PURPOSE:     AMD64 page-table entry regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include <nvs/arch/amd64/archdef.h>

static ULONG Checks, Failures;
static MI_PTE Roots[2][512];

#define CHECK(e) do { Checks++; if (!(e)) { if (Failures++ < 20) fprintf(stderr, "%u: %s\n", __LINE__, #e); } } while (0)

PVOID MiArchMapFrame(ULONG64 Frame) { MI_ASSERT(Frame < 2); return Roots[Frame]; }
VOID MiArchUnmapFrame(PVOID Mapping) { UNREFERENCED_PARAMETER(Mapping); }
MI_PTE MiArchPteRead(PMI_PTE Slot) { return *Slot; }
VOID MiArchPteWrite(PMI_PTE Slot, MI_PTE Value) { *Slot = Value; }

int main(void)
{
    ULONG Protection, Flags, Index;
    MI_PTE Pte, Updated;
    const ULONG64 Frame = 0x12345678;

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
            CHECK(MiArchPteIsExecutable(Pte, TRUE) == !!MI_PROT_IS_EXECUTE(Protection));
            CHECK(MiArchPteIsWritable(Pte) == !!MI_PROT_IS_WRITABLE(Protection));
            CHECK(MiArchPteIsCopyOnWrite(Pte) == !!MI_PROT_IS_COPY(Protection));
            CHECK(!!(Pte & (1ULL << 8)) == !!((Flags & MI_LEAF_GLOBAL) && !(Flags & MI_LEAF_USER)));
            CHECK(!!(Pte & 2) == !!(MI_PROT_IS_WRITABLE(Protection) &&
                                     (Flags & (MI_LEAF_DIRTY | MI_LEAF_HARDWARE_DIRTY))));
            CHECK(MiArchPteIsDirty(Pte) == !!(MI_PROT_IS_WRITABLE(Protection) && (Flags & MI_LEAF_DIRTY)));
            if ((Flags & (MI_LEAF_DEVICE | MI_LEAF_NOCACHE)) || (Protection & MI_PROT_NOCACHE))
                CHECK((Pte & 0x18) == 0x18);
            else if (Flags & MI_LEAF_WRITECOMBINE)
                CHECK((Pte & 0x18) == 0x08);
            else
                CHECK((Pte & 0x18) == 0);
            Updated = MiArchPteSetDirty(Pte, TRUE);
            CHECK(MiArchPteFrame(Updated) == Frame);
            CHECK(MiArchPteIsDirty(Updated) == !!MI_PROT_IS_WRITABLE(Protection));
            Updated = MiArchPteSetDirty(Updated, FALSE);
            CHECK((Updated & (2 | (1ULL << 6))) == 0);
            CHECK(MiArchPteFrame(Updated) == Frame);
            CHECK(!MiArchPteIsAccessed(MiArchPteSetAccessed(Pte, FALSE)));
            CHECK(MiArchPteIsAccessed(MiArchPteSetAccessed(Pte, TRUE)));
        }
    }

    Pte = (1ULL << 11) | (1ULL << 9) | (1ULL << 6) | (1ULL << 5) | 4;
    CHECK(!MiArchPteIsValid(Pte));
    CHECK(!MiArchPteIsWritable(Pte));
    CHECK(!MiArchPteIsCopyOnWrite(Pte));
    CHECK(!MiArchPteIsDirty(Pte));
    CHECK(!MiArchPteIsAccessed(Pte));
    CHECK(!MiArchPteIsUser(Pte));
    CHECK(!MiArchPteIsExecutable(Pte, TRUE));
    CHECK(MiArchPteSetDirty(Pte, TRUE) == Pte);
    CHECK(MiArchPteSetAccessed(Pte, FALSE) == Pte);
    CHECK(MiArchPteSetAccessed(Pte, TRUE) == Pte);
    Pte = MiArchPteMakeBlock(0x40000, MI_PROT_READWRITE, MI_LEAF_GLOBAL | MI_LEAF_DIRTY);
    CHECK(!MiArchPteIsBlock(Pte, 0));
    CHECK(MiArchPteIsBlock(Pte, 1));
    CHECK(MiArchPteIsBlock(Pte, 2));
    CHECK(!MiArchPteIsBlock(Pte, 3));
    CHECK(!MiArchPteIsBlock(Pte, 4));
    CHECK(MiArchPteMakeBlock(0, MI_PROT_NONE, 0) == 0);
    CHECK(MiArchIsSelfMapAddress(0xfffff68000000000ULL));
    CHECK(MiArchIsSelfMapAddress(0xfffff6ffffffffffULL));
    CHECK(!MiArchIsSelfMapAddress(0xfffff70000000000ULL));
    CHECK(!MiArchIsSelfMapAddress(0x0000f68000000000ULL));

    for (Index = 0; Index < 512; Index++)
    {
        Roots[0][Index] = MiArchPteMakeTable(Index + 16, 0);
        Roots[1][Index] = ~(MI_PTE)0;
    }
    MiArchInitializeProcessRoot(0, 1);
    for (Index = 0; Index < 512; Index++)
    {
        if (Index < 256 || Index == MI_AMD64_HYPER_INDEX)
            CHECK(Roots[1][Index] == 0);
        else if (Index == MI_AMD64_SELF_INDEX)
            CHECK(Roots[1][Index] == MiArchPteMakeTable(1, 0));
        else
            CHECK(Roots[1][Index] == Roots[0][Index]);
    }
    printf("AMD64 PTE: %u checks, %u failures\n", Checks, Failures);
    return Failures != 0;
}
