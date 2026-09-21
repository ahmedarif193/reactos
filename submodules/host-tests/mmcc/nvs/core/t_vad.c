/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_vad.c
 * PURPOSE:     Virtual address descriptor host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"

#define VCHECK(c, ...) CHECK(c)

#define MAX_NODES 4000

static MI_VAD_NODE Nodes[MAX_NODES];
static UCHAR Live[MAX_NODES];
static MI_VAD_ROOT Tree;

static ULONG64 Rand64(void)
{
    static ULONG64 State = 0x243F6A8885A308D3ULL;
    State ^= State << 13; State ^= State >> 7; State ^= State << 17;
    return State;
}

static BOOLEAN OverlapsLive(ULONG64 Start, ULONG64 End, ULONG Skip)
{
    ULONG i;
    for (i = 0; i < MAX_NODES; i++)
    {
        if (!Live[i] || i == Skip) continue;
        if (Start <= Nodes[i].EndingVpn && End >= Nodes[i].StartingVpn)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
ReferenceFindEmptyRange(
    _In_ PMI_VAD_ROOT Tree,
    _In_ ULONG64 PageCount,
    _In_ ULONG64 Alignment,
    _In_ ULONG64 LowestVpn,
    _In_ ULONG64 HighestVpn,
    _In_ BOOLEAN TopDown,
    _Out_ PULONG64 StartingVpn)
{
    ULONG64 Mask = (Alignment > 1) ? Alignment - 1 : 0;
    PMI_VAD_NODE Node = MiVadFirst(Tree);
    ULONG64 GapStart;
    BOOLEAN Found = FALSE;

    if (LowestVpn < Tree->LowestVpn)
        LowestVpn = Tree->LowestVpn;

    if (HighestVpn > Tree->HighestVpn)
        HighestVpn = Tree->HighestVpn;

    if (PageCount == 0 || LowestVpn > HighestVpn || HighestVpn - LowestVpn + 1 < PageCount)
        return FALSE;

    GapStart = LowestVpn;

    for (;;)
    {
        ULONG64 GapLast = (Node != NULL) ? Node->StartingVpn - 1 : HighestVpn;
        BOOLEAN Empty = (BOOLEAN)(Node != NULL && Node->StartingVpn == 0);

        if (GapLast > HighestVpn)
            GapLast = HighestVpn;

        if (!Empty && GapLast >= GapStart && GapLast - GapStart + 1 >= PageCount)
        {
            ULONG64 Candidate = TopDown ? ((GapLast - PageCount + 1) & ~Mask) : ((GapStart + Mask) & ~Mask);

            if (Candidate >= GapStart && Candidate <= GapLast && GapLast - Candidate + 1 >= PageCount)
            {
                *StartingVpn = Candidate;
                Found = TRUE;

                if (!TopDown)
                    return TRUE;
            }
        }

        if (Node == NULL || Node->EndingVpn >= HighestVpn)
            break;

        if (Node->EndingVpn + 1 > GapStart)
            GapStart = Node->EndingVpn + 1;

        Node = MiVadNext(Node);
    }

    return Found;
}

void
TestVad(void)
{
    ULONG i, Round;
    ULONG64 Start, Vpn;
    PMI_VAD_NODE Node, Prev;
    ULONG LiveCount = 0;

    MiVadRootInitialize(&Tree, 0x10, 0x7FFFFFFF);

    for (i = 0; i < 1000; i++)
    {
        Nodes[i].StartingVpn = 0x100 + (ULONG64)i * 16;
        Nodes[i].EndingVpn = Nodes[i].StartingVpn + 7;
        VCHECK(MiVadInsert(&Tree, &Nodes[i]), "sequential insert %lu failed", (unsigned long)i);
        Live[i] = 1; LiveCount++;
    }
    VCHECK(MiVadCheck(&Tree) == 0, "tree invalid after sequential inserts");
    VCHECK(Tree.NodeCount == LiveCount, "node count %lu != %lu",
          (unsigned long)Tree.NodeCount, (unsigned long)LiveCount);

    {
        ULONG Seen = 0;
        Prev = NULL;
        for (Node = MiVadFirst(&Tree); Node != NULL; Node = MiVadNext(Node))
        {
            if (Prev) VCHECK(Prev->EndingVpn < Node->StartingVpn,
                            "traversal out of order at %llu",
                            (unsigned long long)Node->StartingVpn);
            Prev = Node; Seen++;
        }
        VCHECK(Seen == LiveCount, "traversal saw %lu of %lu", (unsigned long)Seen,
              (unsigned long)LiveCount);
    }

    for (i = 0; i < 1000; i++)
    {
        VCHECK(MiVadFind(&Tree, Nodes[i].StartingVpn) == &Nodes[i], "find start %lu", (unsigned long)i);
        VCHECK(MiVadFind(&Tree, Nodes[i].EndingVpn) == &Nodes[i], "find end %lu", (unsigned long)i);
        VCHECK(MiVadFind(&Tree, Nodes[i].EndingVpn + 1) != &Nodes[i], "find past end %lu", (unsigned long)i);
    }

    {
        MI_VAD_NODE Clash;
        Clash.StartingVpn = Nodes[10].EndingVpn;
        Clash.EndingVpn = Clash.StartingVpn + 4;
        VCHECK(!MiVadInsert(&Tree, &Clash), "overlapping insert was accepted");
        VCHECK(Tree.NodeCount == LiveCount, "rejected insert changed the count");
    }

    for (i = 0; i < 1000; i += 2)
    {
        MiVadRemove(&Tree, &Nodes[i]);
        Live[i] = 0; LiveCount--;
    }
    VCHECK(MiVadCheck(&Tree) == 0, "tree invalid after strided removal");
    VCHECK(Tree.NodeCount == LiveCount, "count wrong after removal");

    for (Round = 0; Round < 6000; Round++)
    {
        i = (ULONG)(Rand64() % MAX_NODES);

        if (Live[i])
        {
            MiVadRemove(&Tree, &Nodes[i]);
            Live[i] = 0; LiveCount--;
        }
        else
        {
            ULONG64 Len = 1 + (Rand64() % 32);
            Start = 0x100 + (Rand64() % 0x200000);
            if (OverlapsLive(Start, Start + Len - 1, i))
                continue;
            Nodes[i].StartingVpn = Start;
            Nodes[i].EndingVpn = Start + Len - 1;
            if (MiVadInsert(&Tree, &Nodes[i])) { Live[i] = 1; LiveCount++; }
        }

        if ((Round % 97) == 0)
        {
            VCHECK(MiVadCheck(&Tree) == 0, "tree invalid at round %lu", (unsigned long)Round);
            VCHECK(Tree.NodeCount == LiveCount, "count drift at round %lu", (unsigned long)Round);
        }
    }
    VCHECK(MiVadCheck(&Tree) == 0, "tree invalid after randomised rounds");

    for (i = 1; i <= 64; i++)
    {
        if (MiVadFindEmptyRange(&Tree, i, 1, &Start))
            VCHECK(!OverlapsLive(Start, Start + i - 1, MAX_NODES),
                  "bottom-up range of %lu at %llu overlaps",
                  (unsigned long)i, (unsigned long long)Start);

        if (MiVadFindEmptyRangeTopDown(&Tree, i, 1, &Start))
            VCHECK(!OverlapsLive(Start, Start + i - 1, MAX_NODES),
                  "top-down range of %lu at %llu overlaps",
                  (unsigned long)i, (unsigned long long)Start);
    }

    for (i = 0; i < 20000; i++)
    {
        static const ULONG64 Alignments[] = { 1, 1, 4, 16, 256 };
        ULONG64 Count = 1 + (Rand64() % ((i & 1) ? 64 : 0x4000));
        ULONG64 Alignment = Alignments[Rand64() % 5];
        ULONG64 Low = (i % 3) ? 0 : (Rand64() % 0x200000);
        ULONG64 High = (i % 5) ? ~0ULL : (Rand64() % 0x400000);
        BOOLEAN TopDown = (BOOLEAN)((Rand64() & 1) != 0);
        ULONG64 Expected = 0, Actual = 0;
        BOOLEAN ExpectedFound = ReferenceFindEmptyRange(&Tree, Count, Alignment, Low, High, TopDown, &Expected);
        BOOLEAN ActualFound = MiVadFindEmptyRangeEx(&Tree, Count, Alignment, Low, High, TopDown, &Actual);

        VCHECK(ExpectedFound == ActualFound && (!ExpectedFound || Expected == Actual),
               "gap search mismatch count %llu align %llu low %llx high %llx topdown %u: %u/%llx vs %u/%llx",
               (unsigned long long)Count, (unsigned long long)Alignment, (unsigned long long)Low,
               (unsigned long long)High, TopDown, ExpectedFound, (unsigned long long)Expected, ActualFound,
               (unsigned long long)Actual);
    }

    if (getenv("MM_BENCH_VAD") != NULL)
    {
        double Begin;
        ULONG64 Sink = 0;

        Begin = NowSeconds();
        for (i = 0; i < 200000; i++)
        {
            ReferenceFindEmptyRange(&Tree, 0x4000 + (i & 0xFF), 16, 0, ~0ULL, (BOOLEAN)(i & 1), &Start);
            Sink += Start;
        }
        fprintf(stderr, "  vad gap search, %lu nodes, linear:    %8.2fM/s\n", (unsigned long)Tree.NodeCount,
                200000 / (NowSeconds() - Begin) / 1e6);

        Begin = NowSeconds();
        for (i = 0; i < 200000; i++)
        {
            MiVadFindEmptyRangeEx(&Tree, 0x4000 + (i & 0xFF), 16, 0, ~0ULL, (BOOLEAN)(i & 1), &Start);
            Sink += Start;
        }
        fprintf(stderr, "  vad gap search, %lu nodes, augmented: %8.2fM/s (%llu)\n", (unsigned long)Tree.NodeCount,
                200000 / (NowSeconds() - Begin) / 1e6, (unsigned long long)Sink);
    }

    {
        MI_VAD_NODE Fresh;
        ULONG Inserted = 0;
        for (i = 0; i < 64; i++)
        {
            if (!MiVadFindEmptyRange(&Tree, 4, 4, &Start)) break;
            Fresh.StartingVpn = Start; Fresh.EndingVpn = Start + 3;
            VCHECK(Start % 4 == 0, "alignment not honoured: %llu", (unsigned long long)Start);
            if (!MiVadInsert(&Tree, &Fresh)) { VCHECK(0, "found range was not free"); break; }
            MiVadRemove(&Tree, &Fresh);
            Inserted++;
        }
        VCHECK(Inserted > 0, "no empty range could be allocated at all");
    }

    for (i = 0; i < MAX_NODES; i++)
        if (Live[i]) { MiVadRemove(&Tree, &Nodes[i]); Live[i] = 0; }
    VCHECK(Tree.NodeCount == 0, "count %lu after emptying", (unsigned long)Tree.NodeCount);
    VCHECK(Tree.Root == NULL, "root not null after emptying");
    VCHECK(MiVadCheck(&Tree) == 0, "empty tree invalid");
    VCHECK(MiVadFindEmptyRange(&Tree, 16, 1, &Vpn), "empty tree has no free range");
}
