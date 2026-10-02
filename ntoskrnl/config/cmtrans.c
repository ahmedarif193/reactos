/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Configuration Manager: transacted registry operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

#define CMP_TRANS_ABORT_ROUNDS 64
#define CMP_TRANS_MAX_VALUE_NAME 0x8000

typedef struct _CMP_TRANS_VALUE
{
    CM_UOW_SET_VALUE_KEY_DATA Header;
    ULONG Type;
} CMP_TRANS_VALUE, *PCMP_TRANS_VALUE;

ULONG CmpTransUoWCount;

static LIST_ENTRY CmpTransactionListHead;
static LIST_ENTRY CmpTransUoWListHead;
static EX_PUSH_LOCK CmpTransLock;
static KGUARDED_MUTEX CmpTransEnlistLock;
static HANDLE CmpTransTmHandle;
static HANDLE CmpTransRmHandle;
static PKRESOURCEMANAGER CmpTransRm;

static
VOID
CmpTransLockShared(VOID)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&CmpTransLock);
}

static
VOID
CmpTransLockExclusive(VOID)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&CmpTransLock);
}

static
VOID
CmpTransUnlock(VOID)
{
    ExReleasePushLock(&CmpTransLock);
    KeLeaveCriticalRegion();
}

static
PVOID
CmpTransOfKeyBody(
    _In_ PCM_KEY_BODY KeyBody)
{
    return (PVOID)((ULONG_PTR)KeyBody->Trans.TransPtr & ~(ULONG_PTR)1);
}

static
PCM_TRANS
CmpTransFind(
    _In_ PVOID Transaction)
{
    PLIST_ENTRY Entry;
    PCM_TRANS Trans;

    for (Entry = CmpTransactionListHead.Flink;
         Entry != &CmpTransactionListHead;
         Entry = Entry->Flink)
    {
        Trans = CONTAINING_RECORD(Entry, CM_TRANS, TransactionListEntry);
        if (Trans->Trans.TransPtr == Transaction)
        {
            return Trans;
        }
    }
    return NULL;
}

static
BOOLEAN
CmpTransIsActive(
    _In_opt_ PCM_TRANS Trans)
{
    return Trans != NULL &&
           !Trans->Initializing &&
           !Trans->Prepared &&
           !Trans->Committed &&
           !Trans->Aborted;
}

static
BOOLEAN
CmpTransIsValueAction(
    _In_ UoWActionType ActionType)
{
    return ActionType == UoWSetValueNew ||
           ActionType == UoWSetValueExisting ||
           ActionType == UoWDeleteValue;
}

static
VOID
CmpTransValueName(
    _In_ PCMP_TRANS_VALUE Value,
    _Out_ PUNICODE_STRING Name)
{
    Name->Buffer = (PWCHAR)(Value + 1);
    Name->Length = Name->MaximumLength = Value->Header.NameLength;
}

static
PVOID
CmpTransValueData(
    _In_ PCMP_TRANS_VALUE Value)
{
    return (PUCHAR)(Value + 1) + Value->Header.NameLength;
}

static
PCMP_TRANS_VALUE
CmpTransAllocateValue(
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize)
{
    PCMP_TRANS_VALUE Value;
    ULONG Size = sizeof(CMP_TRANS_VALUE) + Name->Length;

    if (DataSize > MAXULONG - Size)
    {
        return NULL;
    }

    Value = ExAllocatePoolWithTag(PagedPool, Size + DataSize, TAG_CM);
    if (Value == NULL)
    {
        return NULL;
    }

    Value->Header.PreparedCell = HCELL_NIL;
    Value->Header.OldValueCell = HCELL_NIL;
    Value->Header.NameLength = Name->Length;
    Value->Header.DataSize = DataSize;
    Value->Type = Type;
    RtlCopyMemory(Value + 1, Name->Buffer, Name->Length);
    if (DataSize != 0)
    {
        RtlCopyMemory(CmpTransValueData(Value), Data, DataSize);
    }
    return Value;
}

static
VOID
CmpTransInsertUoW(
    _Inout_ PCM_TRANS Trans,
    _Inout_ PCM_KCB_UOW UoW,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ UoWActionType ActionType)
{
    UoW->KeyControlBlock = Kcb;
    UoW->Transaction = Trans;
    UoW->ActionType = ActionType;
    InsertTailList(&Trans->KCBUoWListHead, &UoW->TransactionListEntry);
    InsertTailList(&CmpTransUoWListHead, &UoW->KCBListEntry);
    CmpTransUoWCount++;
}

static
VOID
CmpTransRemoveUoW(
    _Inout_ PCM_KCB_UOW UoW)
{
    RemoveEntryList(&UoW->TransactionListEntry);
    RemoveEntryList(&UoW->KCBListEntry);
    CmpTransUoWCount--;
}

static
PCM_KCB_UOW
CmpTransAllocateUoW(VOID)
{
    PCM_KCB_UOW UoW;

    UoW = ExAllocatePoolWithTag(PagedPool, sizeof(CM_KCB_UOW), TAG_CM);
    if (UoW != NULL)
    {
        RtlZeroMemory(UoW, sizeof(*UoW));
    }
    return UoW;
}

static
VOID
CmpTransFreeUoW(
    _In_ PCM_KCB_UOW UoW)
{
    if (CmpTransIsValueAction(UoW->ActionType) && UoW->ValueData != NULL)
    {
        ExFreePoolWithTag(UoW->ValueData, TAG_CM);
    }
    ExFreePoolWithTag(UoW, TAG_CM);
}

static
PCM_KCB_UOW
CmpTransFindKeyUoW(
    _In_ PCM_TRANS Trans,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ UoWActionType ActionType)
{
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;

    for (Entry = Trans->KCBUoWListHead.Flink;
         Entry != &Trans->KCBUoWListHead;
         Entry = Entry->Flink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
        if (UoW->KeyControlBlock == Kcb && UoW->ActionType == ActionType)
        {
            return UoW;
        }
    }
    return NULL;
}

static
PCM_KCB_UOW
CmpTransFindValueUoW(
    _In_ PCM_TRANS Trans,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCUNICODE_STRING Name)
{
    UNICODE_STRING Candidate;
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;

    for (Entry = Trans->KCBUoWListHead.Flink;
         Entry != &Trans->KCBUoWListHead;
         Entry = Entry->Flink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
        if (UoW->KeyControlBlock != Kcb || !CmpTransIsValueAction(UoW->ActionType))
        {
            continue;
        }

        CmpTransValueName((PCMP_TRANS_VALUE)UoW->ValueData, &Candidate);
        if (RtlEqualUnicodeString(&Candidate, Name, TRUE))
        {
            return UoW;
        }
    }
    return NULL;
}

static
NTSTATUS
CmpTransClassifyKcb(
    _In_ PCM_TRANS Trans,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN IncludeChildren,
    _Out_ PBOOLEAN Own)
{
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;

    *Own = FALSE;
    for (Entry = CmpTransUoWListHead.Flink;
         Entry != &CmpTransUoWListHead;
         Entry = Entry->Flink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, KCBListEntry);
        if (UoW->KeyControlBlock == Kcb)
        {
            if (UoW->Transaction != Trans)
            {
                return STATUS_TRANSACTIONAL_CONFLICT;
            }
            if (UoW->ActionType == UoWDeleteThisKey)
            {
                return STATUS_KEY_DELETED;
            }
            if (UoW->ActionType == UoWAddThisKey)
            {
                *Own = TRUE;
            }
        }
        else if (IncludeChildren &&
                 UoW->Transaction != Trans &&
                 UoW->ActionType == UoWAddThisKey &&
                 UoW->KeyControlBlock->ParentKcb == Kcb)
        {
            return STATUS_TRANSACTIONAL_CONFLICT;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
CmpTransCheckActive(
    _In_ PVOID Transaction)
{
    BOOLEAN Active;

    CmpTransLockShared();
    Active = CmpTransIsActive(CmpTransFind(Transaction));
    CmpTransUnlock();

    return Active ? STATUS_SUCCESS : STATUS_TRANSACTION_NOT_ACTIVE;
}

static
NTSTATUS
CmpTransCheckKeyBody(
    _In_ PCM_KEY_BODY KeyBody)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    BOOLEAN Active, Deleted = FALSE;
    PCM_TRANS Trans;

    CmpTransLockShared();
    Trans = CmpTransFind(Transaction);
    Active = CmpTransIsActive(Trans);
    if (Active)
    {
        Deleted = (CmpTransFindKeyUoW(Trans, Kcb, UoWDeleteThisKey) != NULL);
    }
    CmpTransUnlock();

    if (Active)
    {
        return Deleted ? STATUS_KEY_DELETED : STATUS_SUCCESS;
    }
    if (((PKTRANSACTION)Transaction)->Outcome == KTxOutcomeCommitted)
    {
        return STATUS_TRANSACTION_NOT_ACTIVE;
    }
    return Kcb->Delete ? STATUS_KEY_DELETED : STATUS_TRANSACTION_NOT_ACTIVE;
}

BOOLEAN
CmpTransIsCellVisible(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_opt_ PVOID Transaction)
{
    BOOLEAN Visible = TRUE;
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;
    BOOLEAN Same;

    if (CmpTransUoWCount == 0)
    {
        return TRUE;
    }

    CmpTransLockShared();
    for (Entry = CmpTransUoWListHead.Flink;
         Entry != &CmpTransUoWListHead;
         Entry = Entry->Flink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, KCBListEntry);
        if (UoW->KeyControlBlock->KeyHive != Hive || UoW->KeyControlBlock->KeyCell != Cell)
        {
            continue;
        }

        Same = (UoW->Transaction->Trans.TransPtr == Transaction);
        if ((UoW->ActionType == UoWAddThisKey && !Same) ||
            (UoW->ActionType == UoWDeleteThisKey && Same))
        {
            Visible = FALSE;
            break;
        }
    }
    CmpTransUnlock();

    return Visible;
}

BOOLEAN
CmpTransIsKcbVisible(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PVOID Transaction)
{
    PCM_KEY_CONTROL_BLOCK Current;
    BOOLEAN Visible = TRUE;
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;
    BOOLEAN Same;

    if (CmpTransUoWCount == 0)
    {
        return TRUE;
    }

    CmpTransLockShared();
    for (Entry = CmpTransUoWListHead.Flink;
         Entry != &CmpTransUoWListHead && Visible;
         Entry = Entry->Flink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, KCBListEntry);
        Same = (UoW->Transaction->Trans.TransPtr == Transaction);
        if (!((UoW->ActionType == UoWAddThisKey && !Same) ||
              (UoW->ActionType == UoWDeleteThisKey && Same)))
        {
            continue;
        }

        for (Current = Kcb; Current != NULL; Current = Current->ParentKcb)
        {
            if (Current == UoW->KeyControlBlock)
            {
                Visible = FALSE;
                break;
            }
        }
    }
    CmpTransUnlock();

    return Visible;
}

HCELL_INDEX
CmpTransFindSubKeyByNumber(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Parent,
    _In_ ULONG Index,
    _In_opt_ PVOID Transaction)
{
    ULONG Real, Visible = 0;
    HCELL_INDEX Cell;

    if (CmpTransUoWCount == 0)
    {
        return CmpFindSubKeyByNumber(Hive, Parent, Index);
    }

    for (Real = 0; ; Real++)
    {
        Cell = CmpFindSubKeyByNumber(Hive, Parent, Real);
        if (Cell == HCELL_NIL)
        {
            return HCELL_NIL;
        }
        if (!CmpTransIsCellVisible(Hive, Cell, Transaction))
        {
            continue;
        }
        if (Visible == Index)
        {
            return Cell;
        }
        Visible++;
    }
}

VOID
CmpTransAdjustKeyCounts(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PVOID Transaction,
    _Inout_ PULONG SubKeys,
    _Inout_ PULONG Values)
{
    ULONG HiddenKeys = 0, AddedValues = 0, RemovedValues = 0;
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;
    BOOLEAN Same;

    if (CmpTransUoWCount == 0)
    {
        return;
    }

    CmpTransLockShared();
    for (Entry = CmpTransUoWListHead.Flink;
         Entry != &CmpTransUoWListHead;
         Entry = Entry->Flink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, KCBListEntry);
        Same = (UoW->Transaction->Trans.TransPtr == Transaction);
        if (UoW->KeyControlBlock->ParentKcb == Kcb)
        {
            if ((UoW->ActionType == UoWAddThisKey && !Same) ||
                (UoW->ActionType == UoWDeleteThisKey && Same))
            {
                HiddenKeys++;
            }
        }
        else if (UoW->KeyControlBlock == Kcb && Same && Transaction != NULL)
        {
            if (UoW->ActionType == UoWSetValueNew)
            {
                AddedValues++;
            }
            else if (UoW->ActionType == UoWDeleteValue)
            {
                RemovedValues++;
            }
        }
    }
    CmpTransUnlock();

    *SubKeys -= min(*SubKeys, HiddenKeys);
    *Values += AddedValues;
    *Values -= min(*Values, RemovedValues);
}

HSTORAGE_TYPE
CmpTransParentStorage(
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _In_opt_ PVOID Transaction,
    _In_ HSTORAGE_TYPE CellType)
{
    HSTORAGE_TYPE Storage = CellType;
    PCM_TRANS Trans;
    PCM_KCB_UOW UoW;

    if (Transaction == NULL || CmpTransUoWCount == 0)
    {
        return CellType;
    }

    CmpTransLockShared();
    Trans = CmpTransFind(Transaction);
    if (Trans != NULL)
    {
        UoW = CmpTransFindKeyUoW(Trans, ParentKcb, UoWAddThisKey);
        if (UoW != NULL)
        {
            Storage = UoW->StorageType;
        }
    }
    CmpTransUnlock();

    return Storage;
}

NTSTATUS
CmpTransCheckCreate(
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _In_opt_ PVOID Transaction,
    _Out_ PVOID *ConflictEnlistment)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;

    *ConflictEnlistment = NULL;
    if (Transaction == NULL && CmpTransUoWCount == 0)
    {
        return STATUS_SUCCESS;
    }

    CmpTransLockShared();
    if (Transaction != NULL && !CmpTransIsActive(CmpTransFind(Transaction)))
    {
        Status = STATUS_TRANSACTION_NOT_ACTIVE;
    }
    else
    {
        for (Entry = CmpTransUoWListHead.Flink;
             Entry != &CmpTransUoWListHead;
             Entry = Entry->Flink)
        {
            UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, KCBListEntry);
            if (UoW->KeyControlBlock != ParentKcb || UoW->ActionType != UoWDeleteThisKey)
            {
                continue;
            }

            if (UoW->Transaction->Trans.TransPtr == Transaction)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            else
            {
                Status = STATUS_TRANSACTIONAL_CONFLICT;
                if (Transaction == NULL)
                {
                    *ConflictEnlistment = UoW->Transaction->KtmEnlistmentObject;
                    ObReferenceObject(*ConflictEnlistment);
                }
            }
            break;
        }
    }
    CmpTransUnlock();

    return Status;
}

NTSTATUS
CmpTransAddKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PVOID Transaction,
    _In_ HSTORAGE_TYPE StorageType)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PCM_TRANS Trans;
    PCM_KCB_UOW UoW;

    UoW = CmpTransAllocateUoW();
    if (UoW == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    CmpTransLockExclusive();
    Trans = CmpTransFind(Transaction);
    if (!CmpTransIsActive(Trans))
    {
        Status = STATUS_TRANSACTION_NOT_ACTIVE;
    }
    else if (!CmpReferenceKeyControlBlock(Kcb))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        UoW->StorageType = StorageType;
        UoW->VolatileKeyCell = Kcb->KeyCell;
        UoW->ParentUoW = CmpTransFindKeyUoW(Trans, Kcb->ParentKcb, UoWAddThisKey);
        CmpTransInsertUoW(Trans, UoW, Kcb, UoWAddThisKey);
    }
    CmpTransUnlock();

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(UoW, TAG_CM);
    }
    return Status;
}

VOID
CmpTransBindKeyBody(
    _Inout_ PCM_KEY_BODY KeyBody,
    _In_ PVOID Transaction)
{
    if (KeyBody->Type != CM_KEY_BODY_TYPE || KeyBody->Trans.TransPtr != NULL)
    {
        return;
    }

    ObReferenceObject(Transaction);
    KeyBody->Trans.TransPtr = Transaction;
    KeyBody->KtmUow = &((PKTRANSACTION)Transaction)->UOW;
}

VOID
CmpTransUnbindKeyBody(
    _Inout_ PCM_KEY_BODY KeyBody)
{
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);

    if (Transaction != NULL)
    {
        KeyBody->Trans.TransPtr = NULL;
        KeyBody->KtmUow = NULL;
        ObDereferenceObject(Transaction);
    }
}

VOID
CmpTransResolveConflict(
    _In_ PVOID Enlistment)
{
    (VOID)TmRollbackEnlistment(Enlistment, NULL);
    ObDereferenceObject(Enlistment);
}

static
VOID
CmpTransAbortConflicts(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN IncludeChildren)
{
    PKENLISTMENT Enlistment;
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;
    NTSTATUS Status;
    ULONG Round;

    for (Round = 0; Round < CMP_TRANS_ABORT_ROUNDS && CmpTransUoWCount != 0; Round++)
    {
        Enlistment = NULL;

        CmpTransLockShared();
        for (Entry = CmpTransUoWListHead.Flink;
             Entry != &CmpTransUoWListHead;
             Entry = Entry->Flink)
        {
            UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, KCBListEntry);
            if (UoW->KeyControlBlock == Kcb ||
                (IncludeChildren &&
                 UoW->ActionType == UoWAddThisKey &&
                 UoW->KeyControlBlock->ParentKcb == Kcb))
            {
                Enlistment = UoW->Transaction->KtmEnlistmentObject;
                ObReferenceObject(Enlistment);
                break;
            }
        }
        CmpTransUnlock();

        if (Enlistment == NULL)
        {
            break;
        }

        Status = TmRollbackEnlistment(Enlistment, NULL);
        ObDereferenceObject(Enlistment);
        if (!NT_SUCCESS(Status))
        {
            break;
        }
    }
}

static
HCELL_INDEX
CmpTransSecurityAnchor(
    _In_ PHHIVE Hive,
    _In_ HSTORAGE_TYPE Storage)
{
    HCELL_INDEX Anchor = HCELL_NIL;
    PCM_KEY_NODE RootNode;

    if (Hive->BaseBlock->RootCell != HCELL_NIL)
    {
        RootNode = (PCM_KEY_NODE)HvGetCell(Hive, Hive->BaseBlock->RootCell);
        if (RootNode != NULL)
        {
            Anchor = RootNode->Security;
            HvReleaseCell(Hive, Hive->BaseBlock->RootCell);
        }
    }

    if (Anchor != HCELL_NIL && HvGetCellType(Anchor) != (ULONG)Storage)
    {
        Anchor = HCELL_NIL;
    }
    return Anchor;
}

static
HSTORAGE_TYPE
CmpTransShadowStorage(
    _In_ PLIST_ENTRY UoWList,
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_ HSTORAGE_TYPE Default)
{
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;

    for (Entry = UoWList->Flink; Entry != UoWList; Entry = Entry->Flink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
        if (UoW->ActionType == UoWAddThisKey &&
            UoW->KeyControlBlock->KeyHive == Hive &&
            UoW->KeyControlBlock->KeyCell == Cell)
        {
            return UoW->StorageType;
        }
    }
    return Default;
}

static
NTSTATUS
CmpTransCopyKey(
    _In_ PLIST_ENTRY UoWList,
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX SourceCell,
    _In_ HCELL_INDEX ParentCell,
    _In_ HSTORAGE_TYPE Storage,
    _Inout_updates_(2) PHCELL_INDEX Anchors,
    _Out_ PHCELL_INDEX NewCell)
{
    HCELL_INDEX Cell, ClassCell = HCELL_NIL, SecurityCell = HCELL_NIL, Child, NewChild;
    PCM_KEY_NODE Source, Destination;
    ULONG Index, Count;
    NTSTATUS Status;

    *NewCell = HCELL_NIL;

    Source = (PCM_KEY_NODE)HvGetCell(Hive, SourceCell);
    if (Source == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Cell = CmpCopyCell(Hive, SourceCell, Hive, Storage);
    if (Cell == HCELL_NIL)
    {
        HvReleaseCell(Hive, SourceCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Destination = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    ASSERT(Destination != NULL);
    Destination->Parent = ParentCell;
    Destination->Class = HCELL_NIL;
    Destination->Security = HCELL_NIL;
    Destination->ValueList.Count = 0;
    Destination->ValueList.List = HCELL_NIL;
    Destination->SubKeyCounts[Stable] = Destination->SubKeyCounts[Volatile] = 0;
    Destination->SubKeyLists[Stable] = Destination->SubKeyLists[Volatile] = HCELL_NIL;

    Status = STATUS_SUCCESS;
    if (Source->ClassLength > 0 && Source->Class != HCELL_NIL)
    {
        ClassCell = CmpCopyCell(Hive, Source->Class, Hive, Storage);
        if (ClassCell == HCELL_NIL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            Destination->Class = ClassCell;
        }
    }

    if (NT_SUCCESS(Status) && Source->Security != HCELL_NIL)
    {
        CmpLockHiveSecurity(Hive);
        Status = CmpCopyKeySecurity(Hive, Source->Security, Hive, Storage, &Anchors[Storage], &SecurityCell);
        CmpUnlockHiveSecurity(Hive);
        if (NT_SUCCESS(Status))
        {
            Destination->Security = SecurityCell;
        }
    }

    if (NT_SUCCESS(Status))
    {
        Status = CmpCopyKeyValueList(Hive, &Source->ValueList, Hive, &Destination->ValueList, Storage);
    }

    Count = Source->SubKeyCounts[Stable] + Source->SubKeyCounts[Volatile];
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Child = CmpFindSubKeyByNumber(Hive, Source, Index);
        if (Child == HCELL_NIL)
        {
            break;
        }

        Status = CmpTransCopyKey(UoWList,
                                 Hive,
                                 Child,
                                 Cell,
                                 CmpTransShadowStorage(UoWList, Hive, Child, Storage),
                                 Anchors,
                                 &NewChild);
        if (NT_SUCCESS(Status) && !CmpAddSubKey(Hive, Cell, NewChild))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    HvReleaseCell(Hive, Cell);
    HvReleaseCell(Hive, SourceCell);

    if (NT_SUCCESS(Status))
    {
        *NewCell = Cell;
    }
    return Status;
}

static
VOID
CmpTransCommitKey(
    _In_ PLIST_ENTRY UoWList,
    _In_ PCM_KCB_UOW UoW)
{
    PCM_KEY_CONTROL_BLOCK Kcb = UoW->KeyControlBlock;
    PCM_KEY_CONTROL_BLOCK ParentKcb = Kcb->ParentKcb;
    PHHIVE Hive = Kcb->KeyHive;
    HCELL_INDEX Anchors[2], NewCell;
    PCM_KEY_NODE Parent;
    NTSTATUS Status;

    if (Kcb->Delete || ParentKcb == NULL || ParentKcb->Delete)
    {
        return;
    }

    CmpLockHiveFlusherShared((PCMHIVE)Hive);
    Anchors[Stable] = CmpTransSecurityAnchor(Hive, Stable);
    Anchors[Volatile] = CmpTransSecurityAnchor(Hive, Volatile);

    Status = CmpTransCopyKey(UoWList, Hive, Kcb->KeyCell, ParentKcb->KeyCell, Stable, Anchors, &NewCell);
    if (NT_SUCCESS(Status))
    {
        if (!HvMarkCellDirty(Hive, ParentKcb->KeyCell, FALSE) ||
            !CmpAddSubKey(Hive, ParentKcb->KeyCell, NewCell))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (NT_SUCCESS(Status))
    {
        CmpCleanUpSubKeyInfo(ParentKcb);
        Parent = (PCM_KEY_NODE)HvGetCell(Hive, ParentKcb->KeyCell);
        if (Parent != NULL)
        {
            KeQuerySystemTime(&Parent->LastWriteTime);
            ParentKcb->KcbLastWriteTime = Parent->LastWriteTime;
            HvReleaseCell(Hive, ParentKcb->KeyCell);
        }
        CmpReportNotify(ParentKcb, Hive, ParentKcb->KeyCell, REG_NOTIFY_CHANGE_NAME);
    }
    else
    {
        DPRINT1("Committing a transacted key failed with status 0x%lx\n", Status);
    }
    CmpUnlockHiveFlusher((PCMHIVE)Hive);
}

static
BOOLEAN
CmpTransKeepShadow(
    _In_ PCM_KCB_UOW UoW)
{
    while (UoW->ParentUoW != NULL)
    {
        UoW = UoW->ParentUoW;
    }
    return UoW->StorageType == Volatile;
}

static
VOID
CmpTransFinish(
    _In_ PCM_TRANS Trans,
    _In_ BOOLEAN Commit)
{
    UNICODE_STRING Name;
    PCMP_TRANS_VALUE Value;
    LIST_ENTRY UoWList;
    PLIST_ENTRY Entry;
    PCM_KCB_UOW UoW;

    InitializeListHead(&UoWList);

    KeAcquireGuardedMutex(&CmpTransEnlistLock);
    CmpLockRegistryExclusive();
    CmpTransLockExclusive();
    if (Commit)
    {
        Trans->Committed = 1;
    }
    else
    {
        Trans->Aborted = 1;
    }
    RemoveEntryList(&Trans->TransactionListEntry);
    while (!IsListEmpty(&Trans->KCBUoWListHead))
    {
        Entry = RemoveHeadList(&Trans->KCBUoWListHead);
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
        RemoveEntryList(&UoW->KCBListEntry);
        CmpTransUoWCount--;
        InsertTailList(&UoWList, Entry);
    }
    CmpTransUnlock();
    KeReleaseGuardedMutex(&CmpTransEnlistLock);

    if (Commit)
    {
        for (Entry = UoWList.Flink; Entry != &UoWList; Entry = Entry->Flink)
        {
            UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
            switch (UoW->ActionType)
            {
                case UoWAddThisKey:
                    if (UoW->ParentUoW == NULL && UoW->StorageType == Stable)
                    {
                        CmpTransCommitKey(&UoWList, UoW);
                    }
                    break;

                case UoWDeleteThisKey:
                    (VOID)CmDeleteKey(UoW->KeyControlBlock);
                    break;

                case UoWSetValueNew:
                case UoWSetValueExisting:
                    Value = (PCMP_TRANS_VALUE)UoW->ValueData;
                    CmpTransValueName(Value, &Name);
                    (VOID)CmSetValueKey(UoW->KeyControlBlock,
                                        &Name,
                                        Value->Type,
                                        CmpTransValueData(Value),
                                        Value->Header.DataSize);
                    break;

                case UoWDeleteValue:
                    CmpTransValueName((PCMP_TRANS_VALUE)UoW->ValueData, &Name);
                    (VOID)CmDeleteValueKey(UoW->KeyControlBlock, Name);
                    break;

                default:
                    break;
            }
        }
    }

    for (Entry = UoWList.Blink; Entry != &UoWList; Entry = Entry->Blink)
    {
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
        if (UoW->ActionType == UoWAddThisKey && (!Commit || !CmpTransKeepShadow(UoW)))
        {
            (VOID)CmDeleteKey(UoW->KeyControlBlock);
        }
    }

    while (!IsListEmpty(&UoWList))
    {
        Entry = RemoveHeadList(&UoWList);
        UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
        CmpDereferenceKeyControlBlock(UoW->KeyControlBlock);
        CmpTransFreeUoW(UoW);
    }

    CmpUnlockRegistry();
}

static
NTSTATUS
NTAPI
CmpTransNotification(
    _In_ PKENLISTMENT EnlistmentObject,
    _In_ PVOID RMContext,
    _In_ PVOID TransactionContext,
    _In_ ULONG TransactionNotification,
    _Inout_ PLARGE_INTEGER TmVirtualClock,
    _In_ ULONG ArgumentLength,
    _In_ PVOID Argument)
{
    PCM_TRANS Trans = TransactionContext;
    PKENLISTMENT Enlistment;
    HANDLE Handle;

    UNREFERENCED_PARAMETER(RMContext);
    UNREFERENCED_PARAMETER(ArgumentLength);
    UNREFERENCED_PARAMETER(Argument);

    switch (TransactionNotification)
    {
        case TRANSACTION_NOTIFY_PREPREPARE:
            (VOID)TmPrePrepareComplete(EnlistmentObject, TmVirtualClock);
            break;

        case TRANSACTION_NOTIFY_PREPARE:
            CmpTransLockExclusive();
            Trans->Prepared = 1;
            CmpTransUnlock();
            (VOID)TmPrepareComplete(EnlistmentObject, TmVirtualClock);
            break;

        case TRANSACTION_NOTIFY_COMMIT:
        case TRANSACTION_NOTIFY_ROLLBACK:
            CmpTransFinish(Trans, TransactionNotification == TRANSACTION_NOTIFY_COMMIT);
            Handle = Trans->KtmEnlistmentHandle;
            Enlistment = Trans->KtmEnlistmentObject;
            ExFreePoolWithTag(Trans, TAG_CM);

            if (TransactionNotification == TRANSACTION_NOTIFY_COMMIT)
            {
                (VOID)TmCommitComplete(EnlistmentObject, TmVirtualClock);
            }
            else
            {
                (VOID)TmRollbackComplete(EnlistmentObject, TmVirtualClock);
            }
            ObDereferenceObject(Enlistment);
            ObCloseHandle(Handle, KernelMode);
            break;

        default:
            break;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
CmpTransCreateResourceManager(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PKRESOURCEMANAGER ResourceManager;
    HANDLE TmHandle, RmHandle;
    NTSTATUS Status;
    GUID Guid;

    if (CmpTransRm != NULL)
    {
        return STATUS_SUCCESS;
    }

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateTransactionManager(&TmHandle,
                                        TRANSACTIONMANAGER_ALL_ACCESS,
                                        &ObjectAttributes,
                                        NULL,
                                        TRANSACTION_MANAGER_VOLATILE,
                                        0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExUuidCreate(&Guid);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCreateResourceManager(&RmHandle,
                                         RESOURCEMANAGER_ALL_ACCESS,
                                         TmHandle,
                                         &Guid,
                                         &ObjectAttributes,
                                         RESOURCE_MANAGER_VOLATILE,
                                         NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(TmHandle, KernelMode);
        return Status;
    }

    Status = ObReferenceObjectByHandle(RmHandle,
                                       0,
                                       TmResourceManagerObjectType,
                                       KernelMode,
                                       (PVOID *)&ResourceManager,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        Status = TmEnableCallbacks(ResourceManager, CmpTransNotification, NULL);
        if (NT_SUCCESS(Status) &&
            InterlockedCompareExchangePointer((PVOID *)&CmpTransRm, ResourceManager, NULL) == NULL)
        {
            CmpTransTmHandle = TmHandle;
            CmpTransRmHandle = RmHandle;
            return STATUS_SUCCESS;
        }
        ObDereferenceObject(ResourceManager);
    }

    ObCloseHandle(RmHandle, KernelMode);
    ObCloseHandle(TmHandle, KernelMode);
    return Status;
}

NTSTATUS
CmpTransEnlist(
    _In_ PVOID Transaction)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PCM_TRANS Trans;
    NTSTATUS Status;

    Status = CmpTransCreateResourceManager();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireGuardedMutex(&CmpTransEnlistLock);

    CmpTransLockShared();
    Trans = CmpTransFind(Transaction);
    Status = CmpTransIsActive(Trans) ? STATUS_SUCCESS : STATUS_TRANSACTION_NOT_ACTIVE;
    CmpTransUnlock();
    if (Trans != NULL)
    {
        goto Exit;
    }

    Trans = ExAllocatePoolWithTag(PagedPool, sizeof(CM_TRANS), TAG_CM);
    if (Trans == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(Trans, sizeof(*Trans));
    InitializeListHead(&Trans->KCBUoWListHead);
    InitializeListHead(&Trans->LazyCommitListEntry);
    Trans->Initializing = 1;
    Trans->Trans.TransPtr = Transaction;
    TmGetTransactionId(Transaction, &Trans->KtmUow);

    CmpTransLockExclusive();
    InsertTailList(&CmpTransactionListHead, &Trans->TransactionListEntry);
    CmpTransUnlock();

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = TmCreateEnlistment(&Trans->KtmEnlistmentHandle,
                                KernelMode,
                                ENLISTMENT_ALL_ACCESS,
                                &ObjectAttributes,
                                CmpTransRm,
                                Transaction,
                                0,
                                TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                                TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                                Trans);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Trans->KtmEnlistmentHandle,
                                           0,
                                           TmEnlistmentObjectType,
                                           KernelMode,
                                           (PVOID *)&Trans->KtmEnlistmentObject,
                                           NULL);
        ASSERT(NT_SUCCESS(Status));
    }

    CmpTransLockExclusive();
    if (NT_SUCCESS(Status))
    {
        Trans->Initializing = 0;
    }
    else
    {
        RemoveEntryList(&Trans->TransactionListEntry);
    }
    CmpTransUnlock();

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Trans, TAG_CM);
    }

Exit:
    KeReleaseGuardedMutex(&CmpTransEnlistLock);
    return Status;
}

NTSTATUS
CmpTransPrepareWrite(
    _In_ PCM_KEY_BODY KeyBody)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    BOOLEAN Own = FALSE;
    PCM_TRANS Trans;
    NTSTATUS Status;

    if (Transaction == NULL)
    {
        if (CmpTransUoWCount != 0)
        {
            CmpTransAbortConflicts(Kcb, FALSE);
        }
        return STATUS_SUCCESS;
    }

    Status = CmpTransCheckKeyBody(KeyBody);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    CmpTransLockShared();
    Trans = CmpTransFind(Transaction);
    if (Trans != NULL)
    {
        Own = (CmpTransFindKeyUoW(Trans, Kcb, UoWAddThisKey) != NULL);
    }
    CmpTransUnlock();

    return Own ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

static
BOOLEAN
CmpTransRealValueExists(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING ValueName)
{
    KEY_VALUE_BASIC_INFORMATION Information;
    NTSTATUS Status;
    ULONG Length;

    Status = CmQueryValueKey(Kcb,
                             *ValueName,
                             KeyValueBasicInformation,
                             &Information,
                             sizeof(Information),
                             &Length);
    return Status == STATUS_SUCCESS || Status == STATUS_BUFFER_OVERFLOW;
}

NTSTATUS
CmpTransSetValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    PCM_KCB_UOW UoW, Existing;
    PCMP_TRANS_VALUE Value;
    BOOLEAN Exists, Own;
    PCM_TRANS Trans;
    NTSTATUS Status;

    if (Transaction == NULL)
    {
        if (CmpTransUoWCount != 0)
        {
            CmpTransAbortConflicts(Kcb, FALSE);
        }
        return CmSetValueKey(Kcb, ValueName, Type, Data, DataSize);
    }

    Status = CmpTransCheckKeyBody(KeyBody);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Exists = CmpTransRealValueExists(Kcb, ValueName);
    Value = CmpTransAllocateValue(ValueName, Type, Data, DataSize);
    UoW = CmpTransAllocateUoW();
    if (Value == NULL || UoW == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Own = FALSE;
    CmpTransLockExclusive();
    Trans = CmpTransFind(Transaction);
    if (!CmpTransIsActive(Trans))
    {
        Status = STATUS_TRANSACTION_NOT_ACTIVE;
    }
    else
    {
        Status = CmpTransClassifyKcb(Trans, Kcb, FALSE, &Own);
    }

    if (NT_SUCCESS(Status) && !Own)
    {
        Existing = CmpTransFindValueUoW(Trans, Kcb, ValueName);
        if (Existing != NULL)
        {
            ExFreePoolWithTag(Existing->ValueData, TAG_CM);
            Existing->ValueData = &Value->Header;
            if (Existing->ActionType == UoWDeleteValue)
            {
                Existing->ActionType = UoWSetValueExisting;
            }
            Value = NULL;
        }
        else if (!CmpReferenceKeyControlBlock(Kcb))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            UoW->ValueData = &Value->Header;
            CmpTransInsertUoW(Trans, UoW, Kcb, Exists ? UoWSetValueExisting : UoWSetValueNew);
            Value = NULL;
            UoW = NULL;
        }
    }
    CmpTransUnlock();

    if (NT_SUCCESS(Status) && Own)
    {
        Status = CmSetValueKey(Kcb, ValueName, Type, Data, DataSize);
    }

Exit:
    if (Value != NULL)
    {
        ExFreePoolWithTag(Value, TAG_CM);
    }
    if (UoW != NULL)
    {
        ExFreePoolWithTag(UoW, TAG_CM);
    }
    return Status;
}

NTSTATUS
CmpTransDeleteValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PUNICODE_STRING ValueName)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    PCM_KCB_UOW UoW, Existing, Release = NULL;
    PCMP_TRANS_VALUE Value;
    BOOLEAN Exists, Own;
    PCM_TRANS Trans;
    NTSTATUS Status;

    if (Transaction == NULL)
    {
        if (CmpTransUoWCount != 0)
        {
            CmpTransAbortConflicts(Kcb, FALSE);
        }
        return CmDeleteValueKey(Kcb, *ValueName);
    }

    Status = CmpTransCheckKeyBody(KeyBody);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Exists = CmpTransRealValueExists(Kcb, ValueName);
    Value = CmpTransAllocateValue(ValueName, REG_NONE, NULL, 0);
    UoW = CmpTransAllocateUoW();
    if (Value == NULL || UoW == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Own = FALSE;
    CmpTransLockExclusive();
    Trans = CmpTransFind(Transaction);
    if (!CmpTransIsActive(Trans))
    {
        Status = STATUS_TRANSACTION_NOT_ACTIVE;
    }
    else
    {
        Status = CmpTransClassifyKcb(Trans, Kcb, FALSE, &Own);
    }

    if (NT_SUCCESS(Status) && !Own)
    {
        Existing = CmpTransFindValueUoW(Trans, Kcb, ValueName);
        if (Existing == NULL)
        {
            if (!Exists)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            else if (!CmpReferenceKeyControlBlock(Kcb))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                UoW->ValueData = &Value->Header;
                CmpTransInsertUoW(Trans, UoW, Kcb, UoWDeleteValue);
                Value = NULL;
                UoW = NULL;
            }
        }
        else if (Existing->ActionType == UoWDeleteValue)
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
        else if (Existing->ActionType == UoWSetValueNew)
        {
            CmpTransRemoveUoW(Existing);
            Release = Existing;
        }
        else
        {
            ExFreePoolWithTag(Existing->ValueData, TAG_CM);
            Existing->ValueData = &Value->Header;
            Existing->ActionType = UoWDeleteValue;
            Value = NULL;
        }
    }
    CmpTransUnlock();

    if (Release != NULL)
    {
        CmpDereferenceKeyControlBlock(Release->KeyControlBlock);
        CmpTransFreeUoW(Release);
    }

    if (NT_SUCCESS(Status) && Own)
    {
        Status = CmDeleteValueKey(Kcb, *ValueName);
    }

Exit:
    if (Value != NULL)
    {
        ExFreePoolWithTag(Value, TAG_CM);
    }
    if (UoW != NULL)
    {
        ExFreePoolWithTag(UoW, TAG_CM);
    }
    return Status;
}

static
NTSTATUS
CmpTransFormatValue(
    _In_ PCMP_TRANS_VALUE Value,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64 PartialAlign = KeyValueInformation;
    PKEY_VALUE_PARTIAL_INFORMATION Partial = KeyValueInformation;
    PKEY_VALUE_BASIC_INFORMATION Basic = KeyValueInformation;
    PKEY_VALUE_FULL_INFORMATION Full = KeyValueInformation;
    ULONG NameLength = Value->Header.NameLength;
    ULONG DataSize = Value->Header.DataSize;
    PVOID Data = CmpTransValueData(Value);
    ULONG Minimum, Size, DataOffset, Copy;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (KeyValueInformationClass)
    {
        case KeyValueBasicInformation:
            Minimum = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name);
            *ResultLength = Minimum + NameLength;
            if (Length < Minimum)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Basic->TitleIndex = 0;
            Basic->Type = Value->Type;
            Basic->NameLength = NameLength;
            Copy = NameLength;
            if (Length - Minimum < Copy)
            {
                Copy = Length - Minimum;
                Status = STATUS_BUFFER_OVERFLOW;
            }
            RtlCopyMemory(Basic->Name, Value + 1, Copy);
            break;

        case KeyValueFullInformation:
        case KeyValueFullInformationAlign64:
            Minimum = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name);
            DataOffset = Minimum + NameLength;
            if (DataSize != 0)
            {
                if (sizeof(PVOID) == 8 || KeyValueInformationClass == KeyValueFullInformationAlign64)
                {
                    DataOffset = ALIGN_UP(DataOffset, ULONGLONG);
                }
                else
                {
                    DataOffset = ALIGN_UP(DataOffset, ULONG);
                }
            }
            *ResultLength = DataOffset + DataSize;
            if (Length < Minimum)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Full->TitleIndex = 0;
            Full->Type = Value->Type;
            Full->DataLength = DataSize;
            Full->NameLength = NameLength;
            Full->DataOffset = (DataSize != 0) ? DataOffset : MAXULONG;
            Copy = NameLength;
            if (Length - Minimum < Copy)
            {
                Copy = Length - Minimum;
                Status = STATUS_BUFFER_OVERFLOW;
            }
            RtlCopyMemory(Full->Name, Value + 1, Copy);
            if (DataSize != 0)
            {
                Copy = DataSize;
                if (Length < DataOffset)
                {
                    Copy = 0;
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                else if (Length - DataOffset < Copy)
                {
                    Copy = Length - DataOffset;
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                RtlCopyMemory((PUCHAR)Full + DataOffset, Data, Copy);
            }
            break;

        case KeyValuePartialInformation:
            Minimum = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
            *ResultLength = Minimum + DataSize;
            if (Length < Minimum)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Partial->TitleIndex = 0;
            Partial->Type = Value->Type;
            Partial->DataLength = DataSize;
            Copy = DataSize;
            if (Length - Minimum < Copy)
            {
                Copy = Length - Minimum;
                Status = STATUS_BUFFER_OVERFLOW;
            }
            RtlCopyMemory(Partial->Data, Data, Copy);
            break;

        case KeyValuePartialInformationAlign64:
            Minimum = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data);
            *ResultLength = Minimum + DataSize;
            if (Length < Minimum)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            PartialAlign->Type = Value->Type;
            PartialAlign->DataLength = DataSize;
            Copy = DataSize;
            if (Length - Minimum < Copy)
            {
                Copy = Length - Minimum;
                Status = STATUS_BUFFER_OVERFLOW;
            }
            RtlCopyMemory(PartialAlign->Data, Data, Copy);
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    return Status;
}

static
UoWActionType
CmpTransLookupValue(
    _In_ PVOID Transaction,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCUNICODE_STRING ValueName,
    _In_ BOOLEAN Format,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength,
    _Out_ PNTSTATUS FormatStatus)
{
    UoWActionType ActionType = UoWInvalid;
    PCM_TRANS Trans;
    PCM_KCB_UOW UoW;

    *FormatStatus = STATUS_SUCCESS;

    CmpTransLockShared();
    Trans = CmpTransFind(Transaction);
    UoW = (Trans != NULL) ? CmpTransFindValueUoW(Trans, Kcb, ValueName) : NULL;
    if (UoW != NULL)
    {
        ActionType = UoW->ActionType;
        if (Format && ActionType != UoWDeleteValue)
        {
            _SEH2_TRY
            {
                *FormatStatus = CmpTransFormatValue((PCMP_TRANS_VALUE)UoW->ValueData,
                                                    KeyValueInformationClass,
                                                    KeyValueInformation,
                                                    Length,
                                                    ResultLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                *FormatStatus = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
    }
    CmpTransUnlock();

    return ActionType;
}

NTSTATUS
CmpTransQueryValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PUNICODE_STRING ValueName,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    UoWActionType ActionType;
    NTSTATUS Status;

    if (Transaction != NULL)
    {
        Status = CmpTransCheckKeyBody(KeyBody);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        ActionType = CmpTransLookupValue(Transaction,
                                         Kcb,
                                         ValueName,
                                         TRUE,
                                         KeyValueInformationClass,
                                         KeyValueInformation,
                                         Length,
                                         ResultLength,
                                         &Status);
        if (ActionType == UoWDeleteValue)
        {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
        if (ActionType != UoWInvalid)
        {
            return Status;
        }
    }

    return CmQueryValueKey(Kcb,
                           *ValueName,
                           KeyValueInformationClass,
                           KeyValueInformation,
                           Length,
                           ResultLength);
}

static
BOOLEAN
CmpTransHasValueUoW(
    _In_ PVOID Transaction,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Entry;
    PCM_TRANS Trans;
    PCM_KCB_UOW UoW;

    CmpTransLockShared();
    Trans = CmpTransFind(Transaction);
    if (Trans != NULL)
    {
        for (Entry = Trans->KCBUoWListHead.Flink;
             Entry != &Trans->KCBUoWListHead;
             Entry = Entry->Flink)
        {
            UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
            if (UoW->KeyControlBlock == Kcb && CmpTransIsValueAction(UoW->ActionType))
            {
                Found = TRUE;
                break;
            }
        }
    }
    CmpTransUnlock();

    return Found;
}

NTSTATUS
CmpTransEnumerateValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ ULONG Index,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    ULONG BufferSize = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name) + CMP_TRANS_MAX_VALUE_NAME;
    PKEY_VALUE_BASIC_INFORMATION Information;
    ULONG Real, Visible = 0, Needed;
    UoWActionType ActionType;
    NTSTATUS Status, FormatStatus;
    UNICODE_STRING Name;
    PLIST_ENTRY Entry;
    PCM_TRANS Trans;
    PCM_KCB_UOW UoW;
    BOOLEAN Found;

    if (Transaction != NULL)
    {
        Status = CmpTransCheckKeyBody(KeyBody);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    if (Transaction == NULL || !CmpTransHasValueUoW(Transaction, Kcb))
    {
        return CmEnumerateValueKey(Kcb,
                                   Index,
                                   KeyValueInformationClass,
                                   KeyValueInformation,
                                   Length,
                                   ResultLength);
    }

    Information = ExAllocatePoolWithTag(PagedPool, BufferSize, TAG_CM);
    if (Information == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Real = 0; ; Real++)
    {
        Status = CmEnumerateValueKey(Kcb, Real, KeyValueBasicInformation, Information, BufferSize, &Needed);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            goto Exit;
        }

        Name.Buffer = Information->Name;
        Name.Length = Name.MaximumLength = (USHORT)Information->NameLength;
        ActionType = CmpTransLookupValue(Transaction,
                                         Kcb,
                                         &Name,
                                         Visible == Index,
                                         KeyValueInformationClass,
                                         KeyValueInformation,
                                         Length,
                                         ResultLength,
                                         &FormatStatus);
        if (ActionType == UoWDeleteValue)
        {
            continue;
        }
        if (Visible == Index)
        {
            if (ActionType != UoWInvalid)
            {
                Status = FormatStatus;
            }
            else
            {
                Status = CmEnumerateValueKey(Kcb,
                                             Real,
                                             KeyValueInformationClass,
                                             KeyValueInformation,
                                             Length,
                                             ResultLength);
            }
            goto Exit;
        }
        Visible++;
    }

    Found = FALSE;
    Status = STATUS_NO_MORE_ENTRIES;
    CmpTransLockShared();
    Trans = CmpTransFind(Transaction);
    if (Trans != NULL)
    {
        for (Entry = Trans->KCBUoWListHead.Flink;
             Entry != &Trans->KCBUoWListHead && !Found;
             Entry = Entry->Flink)
        {
            UoW = CONTAINING_RECORD(Entry, CM_KCB_UOW, TransactionListEntry);
            if (UoW->KeyControlBlock != Kcb || UoW->ActionType != UoWSetValueNew)
            {
                continue;
            }
            if (Visible != Index)
            {
                Visible++;
                continue;
            }

            Found = TRUE;
            _SEH2_TRY
            {
                Status = CmpTransFormatValue((PCMP_TRANS_VALUE)UoW->ValueData,
                                             KeyValueInformationClass,
                                             KeyValueInformation,
                                             Length,
                                             ResultLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
    }
    CmpTransUnlock();

Exit:
    ExFreePoolWithTag(Information, TAG_CM);
    return Status;
}

NTSTATUS
CmpTransQueryKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    NTSTATUS Status;

    if (Transaction != NULL)
    {
        Status = CmpTransCheckKeyBody(KeyBody);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    return CmQueryKey(KeyBody->KeyControlBlock,
                      Transaction,
                      KeyInformationClass,
                      KeyInformation,
                      Length,
                      ResultLength);
}

NTSTATUS
CmpTransEnumerateKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ ULONG Index,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    NTSTATUS Status;

    if (Transaction != NULL)
    {
        Status = CmpTransCheckKeyBody(KeyBody);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    return CmEnumerateKey(KeyBody->KeyControlBlock,
                          Transaction,
                          Index,
                          KeyInformationClass,
                          KeyInformation,
                          Length,
                          ResultLength);
}

NTSTATUS
CmpTransDeleteKey(
    _In_ PCM_KEY_BODY KeyBody)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PVOID Transaction = CmpTransOfKeyBody(KeyBody);
    UCHAR Buffer[sizeof(KEY_FULL_INFORMATION)];
    PKEY_FULL_INFORMATION Information = (PVOID)Buffer;
    PCM_KCB_UOW UoW, Release = NULL;
    PCM_TRANS Trans;
    NTSTATUS Status;
    BOOLEAN Own;
    ULONG Length;

    if (Transaction == NULL)
    {
        if (CmpTransUoWCount != 0)
        {
            CmpTransAbortConflicts(Kcb, TRUE);
        }
        return CmDeleteKey(Kcb);
    }

    Status = CmpTransCheckKeyBody(KeyBody);
    if (Status == STATUS_KEY_DELETED)
    {
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Kcb->ParentKcb == NULL)
    {
        return STATUS_CANNOT_DELETE;
    }

    Status = CmQueryKey(Kcb, Transaction, KeyFullInformation, Information, sizeof(Buffer), &Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
    {
        return Status;
    }
    if (Information->SubKeys != 0)
    {
        return STATUS_CANNOT_DELETE;
    }

    UoW = CmpTransAllocateUoW();
    if (UoW == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Own = FALSE;
    CmpTransLockExclusive();
    Trans = CmpTransFind(Transaction);
    if (!CmpTransIsActive(Trans))
    {
        Status = STATUS_TRANSACTION_NOT_ACTIVE;
    }
    else
    {
        Status = CmpTransClassifyKcb(Trans, Kcb, TRUE, &Own);
        if (Status == STATUS_KEY_DELETED)
        {
            Status = STATUS_SUCCESS;
            Own = FALSE;
        }
        else if (NT_SUCCESS(Status) && !Own)
        {
            if (!CmpReferenceKeyControlBlock(Kcb))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                CmpTransInsertUoW(Trans, UoW, Kcb, UoWDeleteThisKey);
                UoW = NULL;
            }
        }
    }
    CmpTransUnlock();

    if (UoW != NULL)
    {
        ExFreePoolWithTag(UoW, TAG_CM);
    }

    if (NT_SUCCESS(Status) && Own)
    {
        Status = CmDeleteKey(Kcb);
        if (NT_SUCCESS(Status))
        {
            CmpTransLockExclusive();
            Trans = CmpTransFind(Transaction);
            if (Trans != NULL)
            {
                Release = CmpTransFindKeyUoW(Trans, Kcb, UoWAddThisKey);
                if (Release != NULL)
                {
                    CmpTransRemoveUoW(Release);
                }
            }
            CmpTransUnlock();

            if (Release != NULL)
            {
                CmpDereferenceKeyControlBlock(Release->KeyControlBlock);
                CmpTransFreeUoW(Release);
            }
        }
    }

    return Status;
}

VOID
CmpInitTransactions(VOID)
{
    InitializeListHead(&CmpTransactionListHead);
    InitializeListHead(&CmpTransUoWListHead);
    ExInitializePushLock(&CmpTransLock);
    KeInitializeGuardedMutex(&CmpTransEnlistLock);
}
