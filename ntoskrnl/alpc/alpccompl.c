/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Advanced Local Procedure Call completion lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

#define ALPC_COMPLETION_LIST_ALLOWED_ATTRIBUTES \
    (ALPC_MESSAGE_SECURITY_ATTRIBUTE | ALPC_MESSAGE_CONTEXT_ATTRIBUTE | \
     ALPC_MESSAGE_TOKEN_ATTRIBUTE | ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE)

static ULONG AlpcpAlignUlong(ULONG Value, ULONG Alignment)
{
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

static PVOID
AlpcpCompletionAttributePointer(
    _In_ PALPC_MESSAGE_ATTRIBUTES Attributes,
    _In_ ULONG Flag)
{
    static const struct
    {
        ULONG Flag;
        ULONG Size;
    } Table[] =
    {
        { ALPC_MESSAGE_SECURITY_ATTRIBUTE,       sizeof(ALPC_SECURITY_ATTR) },
        { ALPC_MESSAGE_VIEW_ATTRIBUTE,           sizeof(ALPC_DATA_VIEW_ATTR) },
        { ALPC_MESSAGE_CONTEXT_ATTRIBUTE,        sizeof(ALPC_CONTEXT_ATTR) },
        { ALPC_MESSAGE_HANDLE_ATTRIBUTE,         sizeof(ALPC_HANDLE_ATTR) },
        { ALPC_MESSAGE_TOKEN_ATTRIBUTE,           sizeof(ALPC_TOKEN_ATTR) },
        { ALPC_MESSAGE_DIRECT_ATTRIBUTE,          sizeof(ALPC_DIRECT_ATTR) },
        { ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE, sizeof(ALPC_WORK_ON_BEHALF_ATTR) },
    };
    ULONG Index, Offset = sizeof(*Attributes);

    if (!(Attributes->AllocatedAttributes & Flag)) return NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(Table); Index++)
    {
        if (Table[Index].Flag == Flag) return (PUCHAR)Attributes + Offset;
        if (Attributes->AllocatedAttributes & Table[Index].Flag) Offset += Table[Index].Size;
    }
    return NULL;
}

static NTSTATUS
AlpcpQueryCompletionToken(
    _In_ PKALPC_MESSAGE Message,
    _Out_ PALPC_TOKEN_ATTR TokenAttribute)
{
    NTSTATUS Status;
    PEPROCESS Process;
    PTOKEN Token;
    PTOKEN_STATISTICS Statistics;

    if (Message->Attributes.ValidAttributes & ALPC_MESSAGE_TOKEN_ATTRIBUTE)
    {
        TokenAttribute->TokenId = *(PULONGLONG)&Message->Attributes.TokenId;
        TokenAttribute->AuthenticationId = *(PULONGLONG)&Message->Attributes.AuthenticationId;
        TokenAttribute->ModifiedId = *(PULONGLONG)&Message->Attributes.ModifiedId;
        return STATUS_SUCCESS;
    }

    Process = Message->SenderProcess;
    if (!Process) return STATUS_INVALID_CID;
    ObReferenceObject(Process);
    Token = PsReferencePrimaryToken(Process);
    Status = SeQueryInformationToken(Token, TokenStatistics, (PVOID *)&Statistics);
    PsDereferencePrimaryToken(Token);
    ObDereferenceObject(Process);
    if (!NT_SUCCESS(Status)) return Status;

    TokenAttribute->TokenId = *(PULONGLONG)&Statistics->TokenId;
    TokenAttribute->AuthenticationId = *(PULONGLONG)&Statistics->AuthenticationId;
    TokenAttribute->ModifiedId = *(PULONGLONG)&Statistics->ModifiedId;
    ExFreePoolWithTag(Statistics, TAG_SE);
    return STATUS_SUCCESS;
}

static VOID
AlpcpWriteCompletionAttributes(
    _In_ PKALPC_COMPLETION_LIST CompletionList,
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _Out_ PALPC_MESSAGE_ATTRIBUTES Attributes)
{
    PALPC_CONTEXT_ATTR ContextAttribute;
    PALPC_SECURITY_ATTR SecurityAttribute;
    PALPC_TOKEN_ATTR TokenAttribute;
    PALPC_WORK_ON_BEHALF_ATTR WorkAttribute;
    ULONG Valid = 0;

    RtlZeroMemory(Attributes, CompletionList->AttributeSize);
    Attributes->AllocatedAttributes = CompletionList->AttributeFlags;

    if (CompletionList->AttributeFlags & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
    {
        ContextAttribute = AlpcpCompletionAttributePointer(Attributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
        ContextAttribute->PortContext = Message->PortContext;
        ContextAttribute->MessageContext =
            (AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER) ?
            Message->Attributes.ClientContext : Message->Attributes.ServerContext;
        ContextAttribute->Sequence = Message->Sequence;
        ContextAttribute->MessageId = Message->PortMessage.MessageId;
        ContextAttribute->CallbackId = Message->PortMessage.CallbackId;
        Valid |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    }

    if ((CompletionList->AttributeFlags & ALPC_MESSAGE_SECURITY_ATTRIBUTE) &&
        Message->Attributes.SecurityData)
    {
        SecurityAttribute = AlpcpCompletionAttributePointer(Attributes, ALPC_MESSAGE_SECURITY_ATTRIBUTE);
        SecurityAttribute->Flags = Message->Attributes.SecurityData->Flags;
        SecurityAttribute->QoS = NULL;
        SecurityAttribute->ContextHandle =
            (ALPC_HANDLE)(ULONG_PTR)Message->Attributes.SecurityData->Handle;
        Valid |= ALPC_MESSAGE_SECURITY_ATTRIBUTE;
    }

    if (CompletionList->AttributeFlags & ALPC_MESSAGE_TOKEN_ATTRIBUTE)
    {
        TokenAttribute = AlpcpCompletionAttributePointer(Attributes, ALPC_MESSAGE_TOKEN_ATTRIBUTE);
        if (NT_SUCCESS(AlpcpQueryCompletionToken(Message, TokenAttribute)))
            Valid |= ALPC_MESSAGE_TOKEN_ATTRIBUTE;
    }

    if (CompletionList->AttributeFlags & ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE)
    {
        WorkAttribute = AlpcpCompletionAttributePointer(Attributes, ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE);
        WorkAttribute->Ticket = 0;
        Valid |= ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE;
    }

    Attributes->ValidAttributes = Valid;
}

static BOOLEAN
AlpcpCompletionBitsAreClear(
    _In_ PKALPC_COMPLETION_LIST CompletionList,
    _In_ ULONG Start,
    _In_ ULONG Count)
{
    ULONG Bit;

    for (Bit = Start; Bit < Start + Count; Bit++)
    {
        if ((ULONG)CompletionList->Bitmap[Bit / 32] & (1u << (Bit % 32)))
            return FALSE;
    }
    return TRUE;
}

static VOID
AlpcpSetCompletionBits(
    _In_ PKALPC_COMPLETION_LIST CompletionList,
    _In_ ULONG Start,
    _In_ ULONG Count,
    _In_ BOOLEAN Set)
{
    ULONG Bit;

    for (Bit = Start; Bit < Start + Count; Bit++)
    {
        LONG Mask = (LONG)(1u << (Bit % 32));
        if (Set)
            InterlockedOr(&CompletionList->Bitmap[Bit / 32], Mask);
        else
            InterlockedAnd(&CompletionList->Bitmap[Bit / 32], ~Mask);
    }
}

static LONG
AlpcpAllocateCompletionBits(
    _In_ PKALPC_COMPLETION_LIST CompletionList,
    _In_ ULONG Count)
{
    ULONG Start;

    if (!Count || Count > CompletionList->BitmapLimit) return -1;
    for (Start = 0; Start + Count <= CompletionList->BitmapLimit; Start++)
    {
        if (AlpcpCompletionBitsAreClear(CompletionList, Start, Count))
        {
            AlpcpSetCompletionBits(CompletionList, Start, Count, TRUE);
            return (LONG)Start;
        }
    }
    return -1;
}

BOOLEAN
NTAPI
AlpcpQueueCompletionListMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message)
{
    PKALPC_COMPLETION_LIST CompletionList = Port->CompletionList;
    PKALPC_COMPLETION_LIST_HEADER Header;
    PALPC_MESSAGE_ATTRIBUTES Attributes;
    volatile LONGLONG *StatePointer;
    ULONGLONG State, NewState;
    ULONG Head, Tail, Slot, Capacity;
    ULONG MessageSize, AllocationSize, BitCount, Offset;
    LONG StartBit;

    if (!CompletionList || (Port->Flags & ALPC_PORT_FLAG_COMPLETION_RUNDOWN))
        return FALSE;

    Header = CompletionList->Header;
    MessageSize = (USHORT)Message->PortMessage.u1.s1.TotalLength;
    AllocationSize = MessageSize;
    if (CompletionList->AttributeFlags)
    {
        AllocationSize = AlpcpAlignUlong(AllocationSize, sizeof(ULONGLONG));
        if (AllocationSize > MAXULONG - CompletionList->AttributeSize) return FALSE;
        AllocationSize += CompletionList->AttributeSize;
    }
    BitCount = AlpcpAlignUlong(AllocationSize, ALPC_COMPLETION_LIST_GRANULARITY) /
               ALPC_COMPLETION_LIST_GRANULARITY;
    StartBit = AlpcpAllocateCompletionBits(CompletionList, BitCount);
    if (StartBit < 0) return FALSE;

    Offset = (ULONG)StartBit * ALPC_COMPLETION_LIST_GRANULARITY;
    RtlCopyMemory(CompletionList->Data + Offset, &Message->PortMessage, MessageSize);
    if (CompletionList->AttributeFlags)
    {
        Attributes = (PALPC_MESSAGE_ATTRIBUTES)(CompletionList->Data + Offset +
                     AlpcpAlignUlong(MessageSize, sizeof(ULONGLONG)));
        AlpcpWriteCompletionAttributes(CompletionList, Port, Message, Attributes);
    }

    Capacity = CompletionList->ListSize / sizeof(ULONG);
    StatePointer = &Header->State;
    for (;;)
    {
        State = (ULONGLONG)*StatePointer;
        Head = (ULONG)(State & ALPC_COMPLETION_LIST_INDEX_MASK);
        Tail = (ULONG)((State >> ALPC_COMPLETION_LIST_TAIL_SHIFT) &
                       ALPC_COMPLETION_LIST_INDEX_MASK);
        if (Head == ALPC_COMPLETION_LIST_EMPTY)
        {
            Slot = 0;
            CompletionList->List[Slot] = Offset;
            KeMemoryBarrier();
            NewState = (State & 0xFFFF000000000000ULL) |
                       ((ULONGLONG)Slot << ALPC_COMPLETION_LIST_TAIL_SHIFT) |
                       Slot;
        }
        else
        {
            if (Head >= Capacity || Tail >= Capacity)
                break;
            Slot = (Tail + 1) % Capacity;
            if (Slot == Head)
                break;
            CompletionList->List[Slot] = Offset;
            KeMemoryBarrier();
            NewState = (State & ~(ALPC_COMPLETION_LIST_INDEX_MASK <<
                                  ALPC_COMPLETION_LIST_TAIL_SHIFT)) |
                       ((ULONGLONG)Slot << ALPC_COMPLETION_LIST_TAIL_SHIFT);
        }

        if ((ULONGLONG)InterlockedCompareExchange64(StatePointer, (LONGLONG)NewState, (LONGLONG)State) == State)
        {
            Header->LastMessageId = Message->PortMessage.MessageId;
            Header->LastCallbackId = Message->PortMessage.CallbackId;
            InterlockedIncrement(&Header->PostCount);
            return TRUE;
        }
    }

    AlpcpSetCompletionBits(CompletionList, (ULONG)StartBit, BitCount, FALSE);
    return FALSE;
}

VOID
NTAPI
AlpcpFreeCompletionList(
    _In_ PKALPC_COMPLETION_LIST CompletionList)
{
    if (CompletionList->Mdl)
    {
        MmUnlockPages(CompletionList->Mdl);
        IoFreeMdl(CompletionList->Mdl);
    }
    if (CompletionList->OwnerProcess) ObDereferenceObject(CompletionList->OwnerProcess);
    ExFreePoolWithTag(CompletionList, 'LcpA');
}

NTSTATUS
NTAPI
AlpcpRegisterCompletionList(
    _In_ PALPC_PORT Port,
    _In_ PALPC_PORT_COMPLETION_LIST_INFORMATION Information,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PKALPC_COMPLETION_LIST CompletionList;
    ULONG ListSize, BitmapSize, Remaining, DataBlocks;
    PVOID SystemVa;
    NTSTATUS Status = STATUS_SUCCESS;

    if (((ULONG_PTR)Information->Buffer & (PAGE_SIZE - 1)) ||
        (Information->Size & (PAGE_SIZE - 1)) ||
        Information->Size < 4 * PAGE_SIZE || Information->Size > 0x40000000 ||
        !Information->ConcurrencyCount ||
        (Information->AttributeFlags & ~ALPC_COMPLETION_LIST_ALLOWED_ATTRIBUTES))
        return STATUS_INVALID_PARAMETER;
    if (PsGetCurrentProcess() != Port->OwnerProcess)
        return STATUS_ACCESS_DENIED;

    CompletionList = ExAllocatePoolWithTag(NonPagedPool, sizeof(*CompletionList), 'LcpA');
    if (!CompletionList) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(CompletionList, sizeof(*CompletionList));

    CompletionList->Mdl = IoAllocateMdl(Information->Buffer, Information->Size, FALSE, FALSE, NULL);
    if (!CompletionList->Mdl)
    {
        ExFreePoolWithTag(CompletionList, 'LcpA');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        MmProbeAndLockPages(CompletionList->Mdl, PreviousMode, IoModifyAccess);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(CompletionList->Mdl);
        ExFreePoolWithTag(CompletionList, 'LcpA');
        return Status;
    }

    SystemVa = MmGetSystemAddressForMdlSafe(CompletionList->Mdl, NormalPagePriority);
    if (!SystemVa)
    {
        MmUnlockPages(CompletionList->Mdl);
        IoFreeMdl(CompletionList->Mdl);
        ExFreePoolWithTag(CompletionList, 'LcpA');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(SystemVa, Information->Size);
    ListSize = AlpcpAlignUlong((Information->Size >> 6) * sizeof(ULONGLONG), PAGE_SIZE);
    Remaining = Information->Size - PAGE_SIZE - ListSize;
    DataBlocks = Remaining >> 6;
    BitmapSize = AlpcpAlignUlong((DataBlocks >> 3), PAGE_SIZE);
    if (BitmapSize >= Remaining)
    {
        MmUnlockPages(CompletionList->Mdl);
        IoFreeMdl(CompletionList->Mdl);
        ExFreePoolWithTag(CompletionList, 'LcpA');
        return STATUS_INVALID_PARAMETER;
    }

    ObReferenceObject(Port->OwnerProcess);
    CompletionList->OwnerProcess = Port->OwnerProcess;
    CompletionList->UserVa = Information->Buffer;
    CompletionList->UserLimit = (PUCHAR)Information->Buffer + Information->Size;
    CompletionList->SystemVa = SystemVa;
    CompletionList->TotalSize = Information->Size;
    CompletionList->Header = SystemVa;
    CompletionList->List = (PULONG)((PUCHAR)SystemVa + PAGE_SIZE);
    CompletionList->ListSize = ListSize;
    CompletionList->Bitmap = (volatile LONG *)((PUCHAR)CompletionList->List + ListSize);
    CompletionList->BitmapSize = BitmapSize;
    CompletionList->Data = (PUCHAR)CompletionList->Bitmap + BitmapSize;
    CompletionList->DataSize = Remaining - BitmapSize;
    CompletionList->BitmapLimit = CompletionList->DataSize / ALPC_COMPLETION_LIST_GRANULARITY;
    CompletionList->ConcurrencyCount = Information->ConcurrencyCount;
    CompletionList->AttributeFlags = Information->AttributeFlags;
    CompletionList->AttributeSize = AlpcpAttributesSize(Information->AttributeFlags);

    CompletionList->Header->StartMagic = ALPC_COMPLETION_LIST_START_MAGIC;
    CompletionList->Header->TotalSize = Information->Size;
    CompletionList->Header->ListOffset = PAGE_SIZE;
    CompletionList->Header->ListSize = ListSize;
    CompletionList->Header->BitmapOffset = PAGE_SIZE + ListSize;
    CompletionList->Header->BitmapSize = BitmapSize;
    CompletionList->Header->DataOffset = PAGE_SIZE + ListSize + BitmapSize;
    CompletionList->Header->DataSize = CompletionList->DataSize;
    CompletionList->Header->AttributeFlags = Information->AttributeFlags;
    CompletionList->Header->AttributeSize = CompletionList->AttributeSize;
    CompletionList->Header->State =
        (LONGLONG)((ALPC_COMPLETION_LIST_EMPTY << ALPC_COMPLETION_LIST_TAIL_SHIFT) |
                   ALPC_COMPLETION_LIST_EMPTY);
    CompletionList->Header->EndMagic = ALPC_COMPLETION_LIST_END_MAGIC;
    RtlFillMemoryUlong(CompletionList->List, CompletionList->ListSize, MAXULONG);

    AlpcpAcquireLock();
    if (Port->CompletionList)
    {
        Status = STATUS_PORT_ALREADY_HAS_COMPLETION_LIST;
    }
    else if (Port->Flags & (ALPC_PORT_FLAG_CLOSED | ALPC_PORT_FLAG_DISCONNECTED))
    {
        Status = STATUS_PORT_CLOSED;
    }
    else
    {
        Port->CompletionList = CompletionList;
        Port->Flags |= ALPC_PORT_FLAG_HAS_COMPLETION_LIST;
        Port->Flags &= ~ALPC_PORT_FLAG_COMPLETION_RUNDOWN;
        CompletionList = NULL;
    }
    AlpcpReleaseLock();

    if (CompletionList) AlpcpFreeCompletionList(CompletionList);
    return Status;
}

NTSTATUS
NTAPI
AlpcpAdjustCompletionListConcurrencyCount(
    _In_ PALPC_PORT Port,
    _In_ ULONG ConcurrencyCount)
{
    if (!ConcurrencyCount) return STATUS_INVALID_PARAMETER;

    AlpcpAcquireLock();
    if (!Port->CompletionList)
    {
        AlpcpReleaseLock();
        return STATUS_INVALID_PARAMETER;
    }
    Port->CompletionList->ConcurrencyCount = ConcurrencyCount;
    AlpcpReleaseLock();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AlpcpRundownCompletionList(
    _In_ PALPC_PORT Port)
{
    PKALPC_COMPLETION_LIST CompletionList;
    ULONGLONG State;

    AlpcpAcquireLock();
    CompletionList = Port->CompletionList;
    if (!CompletionList)
    {
        AlpcpReleaseLock();
        return STATUS_INVALID_PARAMETER;
    }
    Port->Flags |= ALPC_PORT_FLAG_COMPLETION_RUNDOWN;
    State = (ULONGLONG)CompletionList->Header->State & 0xFFFF000000000000ULL;
    State |= (ALPC_COMPLETION_LIST_EMPTY << ALPC_COMPLETION_LIST_TAIL_SHIFT) |
             ALPC_COMPLETION_LIST_EMPTY;
    InterlockedExchange64(&CompletionList->Header->State, (LONGLONG)State);
    RtlZeroMemory((PVOID)CompletionList->Bitmap, CompletionList->BitmapSize);
    CompletionList->Header->ReturnCount = CompletionList->Header->PostCount;
    AlpcpReleaseLock();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AlpcpUnregisterCompletionList(
    _In_ PALPC_PORT Port,
    _In_ BOOLEAN Force)
{
    PKALPC_COMPLETION_LIST CompletionList;
    ULONGLONG State;

    AlpcpAcquireLock();
    CompletionList = Port->CompletionList;
    if (!CompletionList)
    {
        AlpcpReleaseLock();
        return STATUS_INVALID_PARAMETER;
    }
    State = (ULONGLONG)CompletionList->Header->State;
    if (!Force && (State >> ALPC_COMPLETION_LIST_ACTIVE_SHIFT))
    {
        AlpcpReleaseLock();
        return STATUS_RESOURCE_IN_USE;
    }
    Port->CompletionList = NULL;
    Port->Flags &= ~(ALPC_PORT_FLAG_HAS_COMPLETION_LIST |
                     ALPC_PORT_FLAG_COMPLETION_RUNDOWN);
    AlpcpReleaseLock();

    AlpcpFreeCompletionList(CompletionList);
    return STATUS_SUCCESS;
}
