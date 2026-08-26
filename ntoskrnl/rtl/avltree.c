/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Low-level RTL AVL tree insertion and removal
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* TYPES *********************************************************************/

typedef struct _RTL_AVL_TREE
{
    PRTL_BALANCED_NODE Root;
} RTL_AVL_TREE, *PRTL_AVL_TREE;

/* PRIVATE FUNCTIONS *********************************************************/

static
PRTL_BALANCED_NODE
RtlpAvlParent(
    _In_ PRTL_BALANCED_NODE Node)
{
    return RTL_BALANCED_NODE_GET_PARENT_POINTER(Node);
}

static
LONG
RtlpAvlBalance(
    _In_ PRTL_BALANCED_NODE Node)
{
    ULONG_PTR Balance;

    Balance = Node->ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK;
    return Balance == RTL_BALANCED_NODE_RESERVED_PARENT_MASK ? -1 : (LONG)Balance;
}

static
VOID
RtlpAvlSetParent(
    _Inout_ PRTL_BALANCED_NODE Node,
    _In_opt_ PRTL_BALANCED_NODE Parent)
{
    Node->ParentValue = (Node->ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK) | (ULONG_PTR)Parent;
}

static
VOID
RtlpAvlSetBalance(
    _Inout_ PRTL_BALANCED_NODE Node,
    _In_ LONG Balance)
{
    ULONG_PTR EncodedBalance;

    ASSERT((Balance >= -1) && (Balance <= 1));
    EncodedBalance = Balance < 0 ? RTL_BALANCED_NODE_RESERVED_PARENT_MASK : (ULONG_PTR)Balance;
    Node->ParentValue = (Node->ParentValue & ~(ULONG_PTR)RTL_BALANCED_NODE_RESERVED_PARENT_MASK) | EncodedBalance;
}

static
VOID
RtlpAvlReplaceChild(
    _Inout_ PRTL_AVL_TREE Tree,
    _In_opt_ PRTL_BALANCED_NODE Parent,
    _In_ PRTL_BALANCED_NODE OldNode,
    _In_opt_ PRTL_BALANCED_NODE NewNode)
{
    if (Parent == NULL)
    {
        Tree->Root = NewNode;
    }
    else
    {
        ASSERT((Parent->Left == OldNode) || (Parent->Right == OldNode));
        Parent->Children[Parent->Right == OldNode] = NewNode;
    }

    if (NewNode != NULL)
        RtlpAvlSetParent(NewNode, Parent);
}

static
PRTL_BALANCED_NODE
RtlpAvlRotateLeft(
    _Inout_ PRTL_AVL_TREE Tree,
    _Inout_ PRTL_BALANCED_NODE Node)
{
    PRTL_BALANCED_NODE Parent;
    PRTL_BALANCED_NODE Right;

    Parent = RtlpAvlParent(Node);
    Right = Node->Right;
    ASSERT(Right != NULL);

    Node->Right = Right->Left;
    if (Node->Right != NULL)
        RtlpAvlSetParent(Node->Right, Node);

    RtlpAvlReplaceChild(Tree, Parent, Node, Right);
    Right->Left = Node;
    RtlpAvlSetParent(Node, Right);
    return Right;
}

static
PRTL_BALANCED_NODE
RtlpAvlRotateRight(
    _Inout_ PRTL_AVL_TREE Tree,
    _Inout_ PRTL_BALANCED_NODE Node)
{
    PRTL_BALANCED_NODE Parent;
    PRTL_BALANCED_NODE Left;

    Parent = RtlpAvlParent(Node);
    Left = Node->Left;
    ASSERT(Left != NULL);

    Node->Left = Left->Right;
    if (Node->Left != NULL)
        RtlpAvlSetParent(Node->Left, Node);

    RtlpAvlReplaceChild(Tree, Parent, Node, Left);
    Left->Right = Node;
    RtlpAvlSetParent(Node, Left);
    return Left;
}

static
PRTL_BALANCED_NODE
RtlpAvlRebalanceRight(
    _Inout_ PRTL_AVL_TREE Tree,
    _Inout_ PRTL_BALANCED_NODE Node,
    _Out_ PBOOLEAN HeightDecreased)
{
    PRTL_BALANCED_NODE Right;
    PRTL_BALANCED_NODE Pivot;
    LONG RightBalance;
    LONG PivotBalance;

    Right = Node->Right;
    RightBalance = RtlpAvlBalance(Right);
    if (RightBalance >= 0)
    {
        Pivot = RtlpAvlRotateLeft(Tree, Node);
        if (RightBalance == 0)
        {
            RtlpAvlSetBalance(Node, 1);
            RtlpAvlSetBalance(Pivot, -1);
            *HeightDecreased = FALSE;
        }
        else
        {
            RtlpAvlSetBalance(Node, 0);
            RtlpAvlSetBalance(Pivot, 0);
            *HeightDecreased = TRUE;
        }
        return Pivot;
    }

    Pivot = Right->Left;
    PivotBalance = RtlpAvlBalance(Pivot);
    RtlpAvlRotateRight(Tree, Right);
    RtlpAvlRotateLeft(Tree, Node);
    RtlpAvlSetBalance(Node, PivotBalance == 1 ? -1 : 0);
    RtlpAvlSetBalance(Right, PivotBalance == -1 ? 1 : 0);
    RtlpAvlSetBalance(Pivot, 0);
    *HeightDecreased = TRUE;
    return Pivot;
}

static
PRTL_BALANCED_NODE
RtlpAvlRebalanceLeft(
    _Inout_ PRTL_AVL_TREE Tree,
    _Inout_ PRTL_BALANCED_NODE Node,
    _Out_ PBOOLEAN HeightDecreased)
{
    PRTL_BALANCED_NODE Left;
    PRTL_BALANCED_NODE Pivot;
    LONG LeftBalance;
    LONG PivotBalance;

    Left = Node->Left;
    LeftBalance = RtlpAvlBalance(Left);
    if (LeftBalance <= 0)
    {
        Pivot = RtlpAvlRotateRight(Tree, Node);
        if (LeftBalance == 0)
        {
            RtlpAvlSetBalance(Node, -1);
            RtlpAvlSetBalance(Pivot, 1);
            *HeightDecreased = FALSE;
        }
        else
        {
            RtlpAvlSetBalance(Node, 0);
            RtlpAvlSetBalance(Pivot, 0);
            *HeightDecreased = TRUE;
        }
        return Pivot;
    }

    Pivot = Left->Right;
    PivotBalance = RtlpAvlBalance(Pivot);
    RtlpAvlRotateLeft(Tree, Left);
    RtlpAvlRotateRight(Tree, Node);
    RtlpAvlSetBalance(Node, PivotBalance == -1 ? 1 : 0);
    RtlpAvlSetBalance(Left, PivotBalance == 1 ? -1 : 0);
    RtlpAvlSetBalance(Pivot, 0);
    *HeightDecreased = TRUE;
    return Pivot;
}

static
VOID
RtlpAvlRebalanceAfterDelete(
    _Inout_ PRTL_AVL_TREE Tree,
    _In_opt_ PRTL_BALANCED_NODE Node,
    _In_ LONG Delta)
{
    PRTL_BALANCED_NODE Parent;
    PRTL_BALANCED_NODE SubtreeRoot;
    BOOLEAN HeightDecreased;
    LONG Balance;

    while (Node != NULL)
    {
        Balance = RtlpAvlBalance(Node) + Delta;
        if ((Balance == -1) || (Balance == 1))
        {
            RtlpAvlSetBalance(Node, Balance);
            break;
        }

        if (Balance == 0)
        {
            RtlpAvlSetBalance(Node, 0);
            SubtreeRoot = Node;
            HeightDecreased = TRUE;
        }
        else if (Balance == 2)
        {
            SubtreeRoot = RtlpAvlRebalanceRight(Tree, Node, &HeightDecreased);
        }
        else
        {
            ASSERT(Balance == -2);
            SubtreeRoot = RtlpAvlRebalanceLeft(Tree, Node, &HeightDecreased);
        }

        if (!HeightDecreased)
            break;

        Parent = RtlpAvlParent(SubtreeRoot);
        if (Parent == NULL)
            break;
        Delta = Parent->Left == SubtreeRoot ? 1 : -1;
        Node = Parent;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
RtlAvlInsertNodeEx(
    _Inout_ PRTL_AVL_TREE Tree,
    _In_opt_ PRTL_BALANCED_NODE Parent,
    _In_ BOOLEAN Right,
    _Inout_ PRTL_BALANCED_NODE Node)
{
    PRTL_BALANCED_NODE Child;
    PRTL_BALANCED_NODE Pivot;
    PRTL_BALANCED_NODE Side;
    LONG Balance;
    LONG SideBalance;

    Node->Left = NULL;
    Node->Right = NULL;
    Node->ParentValue = (ULONG_PTR)Parent;

    if (Parent == NULL)
    {
        Tree->Root = Node;
        return;
    }

    Parent->Children[Right != FALSE] = Node;
    Child = Node;
    while (Parent != NULL)
    {
        Balance = RtlpAvlBalance(Parent) + (Parent->Right == Child ? 1 : -1);
        if (Balance == 0)
        {
            RtlpAvlSetBalance(Parent, 0);
            break;
        }
        if ((Balance == -1) || (Balance == 1))
        {
            RtlpAvlSetBalance(Parent, Balance);
            Child = Parent;
            Parent = RtlpAvlParent(Parent);
            continue;
        }

        if (Balance == 2)
        {
            Side = Parent->Right;
            SideBalance = RtlpAvlBalance(Side);
            if (SideBalance >= 0)
            {
                Pivot = RtlpAvlRotateLeft(Tree, Parent);
                RtlpAvlSetBalance(Parent, 0);
                RtlpAvlSetBalance(Pivot, 0);
            }
            else
            {
                Pivot = Side->Left;
                Balance = RtlpAvlBalance(Pivot);
                RtlpAvlRotateRight(Tree, Side);
                RtlpAvlRotateLeft(Tree, Parent);
                RtlpAvlSetBalance(Parent, Balance == 1 ? -1 : 0);
                RtlpAvlSetBalance(Side, Balance == -1 ? 1 : 0);
                RtlpAvlSetBalance(Pivot, 0);
            }
        }
        else
        {
            ASSERT(Balance == -2);
            Side = Parent->Left;
            SideBalance = RtlpAvlBalance(Side);
            if (SideBalance <= 0)
            {
                Pivot = RtlpAvlRotateRight(Tree, Parent);
                RtlpAvlSetBalance(Parent, 0);
                RtlpAvlSetBalance(Pivot, 0);
            }
            else
            {
                Pivot = Side->Right;
                Balance = RtlpAvlBalance(Pivot);
                RtlpAvlRotateLeft(Tree, Side);
                RtlpAvlRotateRight(Tree, Parent);
                RtlpAvlSetBalance(Parent, Balance == -1 ? 1 : 0);
                RtlpAvlSetBalance(Side, Balance == 1 ? -1 : 0);
                RtlpAvlSetBalance(Pivot, 0);
            }
        }

        break;
    }
}

VOID
NTAPI
RtlAvlRemoveNode(
    _Inout_ PRTL_AVL_TREE Tree,
    _Inout_ PRTL_BALANCED_NODE Node)
{
    PRTL_BALANCED_NODE Parent;
    PRTL_BALANCED_NODE Replacement;
    PRTL_BALANCED_NODE Successor;
    PRTL_BALANCED_NODE SuccessorParent;
    PRTL_BALANCED_NODE SuccessorRight;
    LONG Delta;

    Parent = RtlpAvlParent(Node);
    if ((Node->Left == NULL) || (Node->Right == NULL))
    {
        Replacement = Node->Left != NULL ? Node->Left : Node->Right;
        Delta = (Parent != NULL) && (Parent->Right == Node) ? -1 : 1;
        RtlpAvlReplaceChild(Tree, Parent, Node, Replacement);
        if (Parent != NULL)
            RtlpAvlRebalanceAfterDelete(Tree, Parent, Delta);
        return;
    }

    Successor = Node->Right;
    while (Successor->Left != NULL)
        Successor = Successor->Left;

    SuccessorParent = RtlpAvlParent(Successor);
    SuccessorRight = Successor->Right;
    if (SuccessorParent != Node)
    {
        SuccessorParent->Left = SuccessorRight;
        if (SuccessorRight != NULL)
            RtlpAvlSetParent(SuccessorRight, SuccessorParent);

        Successor->Right = Node->Right;
        RtlpAvlSetParent(Successor->Right, Successor);
        Delta = 1;
    }
    else
    {
        Delta = -1;
    }

    Successor->Left = Node->Left;
    RtlpAvlSetParent(Successor->Left, Successor);
    RtlpAvlSetBalance(Successor, RtlpAvlBalance(Node));
    RtlpAvlReplaceChild(Tree, Parent, Node, Successor);

    if (SuccessorParent == Node)
        RtlpAvlRebalanceAfterDelete(Tree, Successor, Delta);
    else
        RtlpAvlRebalanceAfterDelete(Tree, SuccessorParent, Delta);
}
