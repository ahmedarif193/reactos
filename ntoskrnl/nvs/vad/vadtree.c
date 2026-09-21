/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/vad/vadtree.c
 * PURPOSE:     Virtual address descriptor tree management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/nvsenv.h>
#include <nvs/include/mivad.h>

PMI_VAD_NODE
MiVadFindOverlap(
    _In_ PMI_VAD_ROOT Tree,
    _In_ ULONG64 StartingVpn,
    _In_ ULONG64 EndingVpn)
{
    PMI_VAD_NODE Node = Tree->Root;

    while (Node != NULL)
    {
        if (EndingVpn < Node->StartingVpn)
            Node = Node->Left;
        else if (StartingVpn > Node->EndingVpn)
            Node = Node->Right;
        else
            return Node;
    }

    return NULL;
}

#define MI_VAD_HEIGHT(n) ((n) ? (n)->Height : 0)

static
VOID
MiVadFix(
    _Inout_ PMI_VAD_NODE Node)
{
    ULONG Left = MI_VAD_HEIGHT(Node->Left);
    ULONG Right = MI_VAD_HEIGHT(Node->Right);
    ULONG64 First = Node->StartingVpn;
    ULONG64 Last = Node->EndingVpn;
    ULONG64 MaxGap = 0;
    ULONG64 Gap;

    if (Node->Left != NULL)
    {
        First = Node->Left->SubtreeFirstVpn;
        MaxGap = Node->Left->MaxGap;
        Gap = Node->StartingVpn - Node->Left->SubtreeLastVpn - 1;
        if (Gap > MaxGap)
            MaxGap = Gap;
    }

    if (Node->Right != NULL)
    {
        Last = Node->Right->SubtreeLastVpn;
        if (Node->Right->MaxGap > MaxGap)
            MaxGap = Node->Right->MaxGap;
        Gap = Node->Right->SubtreeFirstVpn - Node->EndingVpn - 1;
        if (Gap > MaxGap)
            MaxGap = Gap;
    }

    Node->Height = 1 + (Left > Right ? Left : Right);
    Node->SubtreeFirstVpn = First;
    Node->SubtreeLastVpn = Last;
    Node->MaxGap = MaxGap;
}

static
LONG
MiVadBalance(
    _In_ PMI_VAD_NODE Node)
{
    return (LONG)MI_VAD_HEIGHT(Node->Left) - (LONG)MI_VAD_HEIGHT(Node->Right);
}

static
VOID
MiVadReplaceChild(
    _Inout_ PMI_VAD_ROOT Tree,
    _In_ PMI_VAD_NODE Parent,
    _In_ PMI_VAD_NODE Old,
    _In_ PMI_VAD_NODE New)
{
    if (Parent == NULL)
        Tree->Root = New;
    else if (Parent->Left == Old)
        Parent->Left = New;
    else
        Parent->Right = New;

    if (New != NULL)
        New->Parent = Parent;
}

static
VOID
MiVadRotateLeft(
    _Inout_ PMI_VAD_ROOT Tree,
    _Inout_ PMI_VAD_NODE Node)
{
    PMI_VAD_NODE Right = Node->Right;

    MiVadReplaceChild(Tree, Node->Parent, Node, Right);

    Node->Right = Right->Left;
    if (Node->Right != NULL)
        Node->Right->Parent = Node;

    Right->Left = Node;
    Node->Parent = Right;

    MiVadFix(Node);
    MiVadFix(Right);
}

static
VOID
MiVadRotateRight(
    _Inout_ PMI_VAD_ROOT Tree,
    _Inout_ PMI_VAD_NODE Node)
{
    PMI_VAD_NODE Left = Node->Left;

    MiVadReplaceChild(Tree, Node->Parent, Node, Left);

    Node->Left = Left->Right;
    if (Node->Left != NULL)
        Node->Left->Parent = Node;

    Left->Right = Node;
    Node->Parent = Left;

    MiVadFix(Node);
    MiVadFix(Left);
}

static
VOID
MiVadRebalance(
    _Inout_ PMI_VAD_ROOT Tree,
    _Inout_ PMI_VAD_NODE Start)
{
    PMI_VAD_NODE Node = Start;
    LONG Balance;

    while (Node != NULL)
    {
        PMI_VAD_NODE Parent = Node->Parent;

        MiVadFix(Node);
        Balance = MiVadBalance(Node);

        if (Balance > 1)
        {
            if (MiVadBalance(Node->Left) < 0)
                MiVadRotateLeft(Tree, Node->Left);
            MiVadRotateRight(Tree, Node);
        }
        else if (Balance < -1)
        {
            if (MiVadBalance(Node->Right) > 0)
                MiVadRotateRight(Tree, Node->Right);
            MiVadRotateLeft(Tree, Node);
        }

        Node = Parent;
    }
}

VOID
MiVadRootInitialize(
    _Out_ PMI_VAD_ROOT Tree,
    _In_ ULONG64 LowestVpn,
    _In_ ULONG64 HighestVpn)
{
    Tree->Root = NULL;
    Tree->NodeCount = 0;
    Tree->LowestVpn = LowestVpn;
    Tree->HighestVpn = HighestVpn;
}

BOOLEAN
MiVadInsert(
    _Inout_ PMI_VAD_ROOT Tree,
    _Inout_ PMI_VAD_NODE Node)
{
    PMI_VAD_NODE Current = Tree->Root;
    PMI_VAD_NODE Parent = NULL;

    if (Node->StartingVpn > Node->EndingVpn)
        return FALSE;

    Node->Left = NULL;
    Node->Right = NULL;
    Node->Parent = NULL;
    Node->Height = 1;
    Node->SubtreeFirstVpn = Node->StartingVpn;
    Node->SubtreeLastVpn = Node->EndingVpn;
    Node->MaxGap = 0;

    while (Current != NULL)
    {
        Parent = Current;

        if (Node->EndingVpn < Current->StartingVpn)
            Current = Current->Left;
        else if (Node->StartingVpn > Current->EndingVpn)
            Current = Current->Right;
        else
            return FALSE;
    }

    Node->Parent = Parent;
    if (Parent == NULL)
        Tree->Root = Node;
    else if (Node->EndingVpn < Parent->StartingVpn)
        Parent->Left = Node;
    else
        Parent->Right = Node;

    Tree->NodeCount++;
    MiVadRebalance(Tree, Parent);
    return TRUE;
}

static
PMI_VAD_NODE
MiVadMinimum(
    _In_ PMI_VAD_NODE Node)
{
    while (Node->Left != NULL)
        Node = Node->Left;

    return Node;
}

VOID
MiVadRemove(
    _Inout_ PMI_VAD_ROOT Tree,
    _Inout_ PMI_VAD_NODE Node)
{
    PMI_VAD_NODE Start;
    PMI_VAD_NODE Successor;

    if (Node->Left == NULL || Node->Right == NULL)
    {
        Start = Node->Parent;
        MiVadReplaceChild(Tree, Node->Parent, Node,
                          Node->Left != NULL ? Node->Left : Node->Right);
        Tree->NodeCount--;
        MiVadRebalance(Tree, Start);
        return;
    }

    Successor = MiVadMinimum(Node->Right);

    if (Successor->Parent == Node)
    {
        Start = Successor;
    }
    else
    {
        Start = Successor->Parent;
        MiVadReplaceChild(Tree, Successor->Parent, Successor, Successor->Right);
        Successor->Right = Node->Right;
        if (Successor->Right != NULL)
            Successor->Right->Parent = Successor;
    }

    Successor->Left = Node->Left;
    if (Successor->Left != NULL)
        Successor->Left->Parent = Successor;

    MiVadReplaceChild(Tree, Node->Parent, Node, Successor);
    Successor->Height = Node->Height;

    Tree->NodeCount--;
    MiVadRebalance(Tree, Start);
}

PMI_VAD_NODE
MiVadFind(
    _In_ PMI_VAD_ROOT Tree,
    _In_ ULONG64 Vpn)
{
    PMI_VAD_NODE Node = Tree->Root;

    while (Node != NULL)
    {
        if (Vpn < Node->StartingVpn)
            Node = Node->Left;
        else if (Vpn > Node->EndingVpn)
            Node = Node->Right;
        else
            return Node;
    }

    return NULL;
}

PMI_VAD_NODE
MiVadFirst(
    _In_ PMI_VAD_ROOT Tree)
{
    if (Tree->Root == NULL)
        return NULL;

    return MiVadMinimum(Tree->Root);
}

PMI_VAD_NODE
MiVadNext(
    _In_ PMI_VAD_NODE Node)
{
    PMI_VAD_NODE Parent;

    if (Node->Right != NULL)
        return MiVadMinimum(Node->Right);

    Parent = Node->Parent;
    while (Parent != NULL && Parent->Right == Node)
    {
        Node = Parent;
        Parent = Parent->Parent;
    }

    return Parent;
}

typedef struct _MI_VAD_GAP_REQUEST
{
    ULONG64 PageCount;
    ULONG64 Mask;
    ULONG64 LowestVpn;
    ULONG64 HighestVpn;
    BOOLEAN TopDown;
} MI_VAD_GAP_REQUEST, *PMI_VAD_GAP_REQUEST;

static
BOOLEAN
MiVadTryGap(
    _In_ const MI_VAD_GAP_REQUEST *Request,
    _In_ ULONG64 GapStart,
    _In_ ULONG64 GapLast,
    _Out_ PULONG64 StartingVpn)
{
    ULONG64 Candidate;

    if (GapStart < Request->LowestVpn)
        GapStart = Request->LowestVpn;

    if (GapLast > Request->HighestVpn)
        GapLast = Request->HighestVpn;

    if (GapLast < GapStart || GapLast - GapStart + 1 < Request->PageCount)
        return FALSE;

    Candidate = Request->TopDown ? ((GapLast - Request->PageCount + 1) & ~Request->Mask)
                                 : ((GapStart + Request->Mask) & ~Request->Mask);

    if (Candidate < GapStart || Candidate > GapLast || GapLast - Candidate + 1 < Request->PageCount)
        return FALSE;

    *StartingVpn = Candidate;
    return TRUE;
}

static
BOOLEAN
MiVadSearchGap(
    _In_opt_ PMI_VAD_NODE Node,
    _In_ const MI_VAD_GAP_REQUEST *Request,
    _In_ ULONG64 Low,
    _In_ ULONG64 High,
    _Out_ PULONG64 StartingVpn)
{
    ULONG64 Best;
    ULONG Pass;

    if (Node == NULL)
        return (BOOLEAN)(Low <= High && MiVadTryGap(Request, Low, High, StartingVpn));

    if (High < Request->LowestVpn || Low > Request->HighestVpn)
        return FALSE;

    Best = Node->MaxGap;
    if (Node->SubtreeFirstVpn - Low > Best)
        Best = Node->SubtreeFirstVpn - Low;
    if (High - Node->SubtreeLastVpn > Best)
        Best = High - Node->SubtreeLastVpn;

    if (Best < Request->PageCount)
        return FALSE;

    for (Pass = 0; Pass < 2; Pass++)
    {
        if ((Pass == 0) != (Request->TopDown != FALSE))
        {
            if (Node->StartingVpn != 0 &&
                MiVadSearchGap(Node->Left, Request, Low, Node->StartingVpn - 1, StartingVpn))
            {
                return TRUE;
            }
        }
        else
        {
            if (Node->EndingVpn != ~0ULL &&
                MiVadSearchGap(Node->Right, Request, Node->EndingVpn + 1, High, StartingVpn))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

BOOLEAN
MiVadFindEmptyRangeEx(
    _In_ PMI_VAD_ROOT Tree,
    _In_ ULONG64 PageCount,
    _In_ ULONG64 Alignment,
    _In_ ULONG64 LowestVpn,
    _In_ ULONG64 HighestVpn,
    _In_ BOOLEAN TopDown,
    _Out_ PULONG64 StartingVpn)
{
    MI_VAD_GAP_REQUEST Request;

    if (LowestVpn < Tree->LowestVpn)
        LowestVpn = Tree->LowestVpn;

    if (HighestVpn > Tree->HighestVpn)
        HighestVpn = Tree->HighestVpn;

    if (PageCount == 0 || LowestVpn > HighestVpn || HighestVpn - LowestVpn + 1 < PageCount)
        return FALSE;

    Request.PageCount = PageCount;
    Request.Mask = (Alignment > 1) ? Alignment - 1 : 0;
    Request.LowestVpn = LowestVpn;
    Request.HighestVpn = HighestVpn;
    Request.TopDown = TopDown;

    return MiVadSearchGap(Tree->Root, &Request, Tree->LowestVpn, Tree->HighestVpn, StartingVpn);
}

BOOLEAN
MiVadFindEmptyRange(
    _In_ PMI_VAD_ROOT Tree,
    _In_ ULONG64 PageCount,
    _In_ ULONG64 Alignment,
    _Out_ PULONG64 StartingVpn)
{
    return MiVadFindEmptyRangeEx(Tree, PageCount, Alignment, 0, ~0ULL, FALSE, StartingVpn);
}

BOOLEAN
MiVadFindEmptyRangeTopDown(
    _In_ PMI_VAD_ROOT Tree,
    _In_ ULONG64 PageCount,
    _In_ ULONG64 Alignment,
    _Out_ PULONG64 StartingVpn)
{
    return MiVadFindEmptyRangeEx(Tree, PageCount, Alignment, 0, ~0ULL, TRUE, StartingVpn);
}

static
ULONG
MiVadCheckNode(
    _In_ PMI_VAD_NODE Node,
    _In_ PMI_VAD_NODE Parent,
    _Inout_ PULONG Errors,
    _Inout_ PULONG64 Count)
{
    ULONG Left, Right;
    LONG Balance;

    if (Node == NULL)
        return 0;

    if (Node->Parent != Parent)
        (*Errors)++;
    if (Node->StartingVpn > Node->EndingVpn)
        (*Errors)++;
    if (Node->Left != NULL && Node->Left->EndingVpn >= Node->StartingVpn)
        (*Errors)++;
    if (Node->Right != NULL && Node->Right->StartingVpn <= Node->EndingVpn)
        (*Errors)++;

    Left = MiVadCheckNode(Node->Left, Node, Errors, Count);
    Right = MiVadCheckNode(Node->Right, Node, Errors, Count);

    Balance = (LONG)Left - (LONG)Right;
    if (Balance > 1 || Balance < -1)
        (*Errors)++;

    if (Node->Height != 1 + (Left > Right ? Left : Right))
        (*Errors)++;

    {
        MI_VAD_NODE Expected = *Node;

        MiVadFix(&Expected);
        if (Expected.SubtreeFirstVpn != Node->SubtreeFirstVpn || Expected.SubtreeLastVpn != Node->SubtreeLastVpn ||
            Expected.MaxGap != Node->MaxGap)
        {
            (*Errors)++;
        }
    }

    (*Count)++;
    return 1 + (Left > Right ? Left : Right);
}

ULONG
MiVadCheck(
    _In_ PMI_VAD_ROOT Tree)
{
    ULONG Errors = 0;
    ULONG64 Count = 0;

    MiVadCheckNode(Tree->Root, NULL, &Errors, &Count);

    if (Count != Tree->NodeCount)
        Errors++;

    return Errors;
}
