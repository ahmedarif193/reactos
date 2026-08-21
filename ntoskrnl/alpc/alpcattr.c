/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Advanced Local Procedure Call message attributes and resources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

#define ALPC_VIEWFLG_VALID (ALPC_VIEWFLG_UNMAP_EXISTING | ALPC_VIEWFLG_AUTO_RELEASE | ALPC_VIEWFLG_NOT_SECURE)

static const struct
{
    ULONG Flag;
    ULONG Size;
} AlpcpAttributeTable[] =
{
    { ALPC_MESSAGE_SECURITY_ATTRIBUTE,       sizeof(ALPC_SECURITY_ATTR) },
    { ALPC_MESSAGE_VIEW_ATTRIBUTE,           sizeof(ALPC_DATA_VIEW_ATTR) },
    { ALPC_MESSAGE_CONTEXT_ATTRIBUTE,        sizeof(ALPC_CONTEXT_ATTR) },
    { ALPC_MESSAGE_HANDLE_ATTRIBUTE,         sizeof(ALPC_HANDLE_ATTR) },
    { ALPC_MESSAGE_TOKEN_ATTRIBUTE,          sizeof(ALPC_TOKEN_ATTR) },
    { ALPC_MESSAGE_DIRECT_ATTRIBUTE,         sizeof(ALPC_DIRECT_ATTR) },
    { ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE, sizeof(ALPC_WORK_ON_BEHALF_ATTR) },
};

ULONG
NTAPI
AlpcpAttributesSize(
    _In_ ULONG AllocatedAttributes)
{
    ULONG i, Size = sizeof(ALPC_MESSAGE_ATTRIBUTES);

    for (i = 0; i < RTL_NUMBER_OF(AlpcpAttributeTable); i++)
    {
        if (AllocatedAttributes & AlpcpAttributeTable[i].Flag) Size += AlpcpAttributeTable[i].Size;
    }
    return Size;
}

static
PVOID
AlpcpAttributePointer(
    _In_ PALPC_MESSAGE_ATTRIBUTES Attributes,
    _In_ ULONG Flag)
{
    ULONG i, Offset = sizeof(ALPC_MESSAGE_ATTRIBUTES);

    if (!(Attributes->AllocatedAttributes & Flag)) return NULL;
    for (i = 0; i < RTL_NUMBER_OF(AlpcpAttributeTable); i++)
    {
        if (AlpcpAttributeTable[i].Flag == Flag) return (PUCHAR)Attributes + Offset;
        if (Attributes->AllocatedAttributes & AlpcpAttributeTable[i].Flag) Offset += AlpcpAttributeTable[i].Size;
    }
    return NULL;
}

static
PVOID
AlpcpMessageContextForPort(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message)
{
    if (AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER)
        return Message->Attributes.ClientContext;

    return Message->Attributes.ServerContext;
}

NTSTATUS
NTAPI
AlpcpCaptureAttributes(
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES UserAttributes,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PALPC_MESSAGE_ATTRIBUTES *Captured)
{
    ALPC_MESSAGE_ATTRIBUTES Header;
    PALPC_MESSAGE_ATTRIBUTES Buffer;
    ULONG Size;

    *Captured = NULL;
    if (!UserAttributes) return STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(UserAttributes, sizeof(Header), sizeof(ULONG));
            Header = *(volatile ALPC_MESSAGE_ATTRIBUTES*)UserAttributes;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Header = *UserAttributes;
    }

    if (Header.AllocatedAttributes & ~ALPC_MESSAGE_ATTRIBUTE_ALL) return STATUS_INVALID_PARAMETER;
    if (Header.ValidAttributes & ~Header.AllocatedAttributes) return STATUS_INVALID_PARAMETER;

    Size = AlpcpAttributesSize(Header.AllocatedAttributes);
    Buffer = ExAllocatePoolWithTag(PagedPool, Size, 'AcpA');
    if (!Buffer) return STATUS_NO_MEMORY;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(UserAttributes, Size, sizeof(ULONG));
            RtlCopyMemory(Buffer, UserAttributes, Size);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(Buffer, 'AcpA');
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Buffer, UserAttributes, Size);
    }

    *Captured = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AlpcpWriteAttributes(
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES UserAttributes,
    _In_ PALPC_MESSAGE_ATTRIBUTES Captured,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Size;

    if (!UserAttributes || !Captured) return STATUS_SUCCESS;
    Size = AlpcpAttributesSize(Captured->AllocatedAttributes);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(UserAttributes, Size, sizeof(ULONG));
            RtlCopyMemory(UserAttributes, Captured, Size);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(UserAttributes, Captured, Size);
    }
    return STATUS_SUCCESS;
}

PKALPC_SECTION
NTAPI
AlpcpLookupSection(
    _In_ PALPC_PORT Port,
    _In_ ULONG Handle)
{
    PLIST_ENTRY Entry;
    PKALPC_SECTION Section;

    for (Entry = Port->SectionList.Flink; Entry != &Port->SectionList; Entry = Entry->Flink)
    {
        Section = CONTAINING_RECORD(Entry, KALPC_SECTION, Entry);
        if (Section->Handle == Handle) return Section;
    }
    return NULL;
}

PKALPC_VIEW
NTAPI
AlpcpLookupView(
    _In_ PALPC_PORT Port,
    _In_ PVOID Address)
{
    PLIST_ENTRY Entry;
    PKALPC_VIEW View;

    for (Entry = Port->ViewList.Flink; Entry != &Port->ViewList; Entry = Entry->Flink)
    {
        View = CONTAINING_RECORD(Entry, KALPC_VIEW, Entry);
        if (!View->DeletePending && (View->Address == Address) && (View->Process == PsGetCurrentProcess())) return View;
    }
    return NULL;
}

static
PKALPC_VIEW
AlpcpFindViewBySection(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_SECTION Section)
{
    PLIST_ENTRY Entry;
    PKALPC_VIEW View;

    for (Entry = Port->ViewList.Flink; Entry != &Port->ViewList; Entry = Entry->Flink)
    {
        View = CONTAINING_RECORD(Entry, KALPC_VIEW, Entry);
        if (!View->DeletePending && (View->Section == Section) && (View->Process == PsGetCurrentProcess())) return View;
    }
    return NULL;
}

PKALPC_SECURITY_DATA
NTAPI
AlpcpLookupSecurityData(
    _In_ PALPC_PORT Port,
    _In_ ULONG Handle)
{
    PLIST_ENTRY Entry;
    PKALPC_SECURITY_DATA Data;

    for (Entry = Port->SecurityList.Flink; Entry != &Port->SecurityList; Entry = Entry->Flink)
    {
        Data = CONTAINING_RECORD(Entry, KALPC_SECURITY_DATA, Entry);
        if (Data->Handle == Handle) return Data;
    }
    return NULL;
}

VOID
NTAPI
AlpcpDereferenceSection(
    _In_ PKALPC_SECTION Section)
{
    PVOID Object;

    if (InterlockedDecrement(&Section->ReferenceCount) != 0) return;
    Object = Section->SectionObject;
    ExFreePoolWithTag(Section, 'ScpA');
    if (Object) ObDereferenceObject(Object);
}

VOID
NTAPI
AlpcpDereferenceView(
    _In_ PKALPC_VIEW View)
{
    LONG ReferenceCount;
    BOOLEAN DropListReference = FALSE;
    PEPROCESS Process;
    PKALPC_SECTION Section;

    AlpcpAcquireLock();
    ReferenceCount = InterlockedDecrement(&View->ReferenceCount);
    if (ReferenceCount == 1 && View->DeletePending)
    {
        if (View->DeletePending && View->ReferenceCount == 1 && !IsListEmpty(&View->Entry))
        {
            RemoveEntryList(&View->Entry);
            InitializeListHead(&View->Entry);
            if (View->Section) InterlockedDecrement(&View->Section->ViewCount);
            DropListReference = TRUE;
        }
        if (DropListReference) ReferenceCount = InterlockedDecrement(&View->ReferenceCount);
    }
    AlpcpReleaseLock();
    if (ReferenceCount != 0) return;

    Process = View->Process;
    Section = View->Section;
    if (View->Address && Process)
    {
        MmUnmapViewOfSection(Process, View->Address);
    }
    ExFreePoolWithTag(View, 'VcpA');
    if (Process) ObDereferenceObject(Process);
    if (Section) AlpcpDereferenceSection(Section);
}

VOID
NTAPI
AlpcpDeleteView(
    _In_ PKALPC_VIEW View)
{
    BOOLEAN DropListReference = FALSE;

    AlpcpAcquireLock();
    View->DeletePending = TRUE;
    if (View->ReferenceCount == 1 && !IsListEmpty(&View->Entry))
    {
        RemoveEntryList(&View->Entry);
        InitializeListHead(&View->Entry);
        if (View->Section) InterlockedDecrement(&View->Section->ViewCount);
        DropListReference = TRUE;
    }
    AlpcpReleaseLock();

    if (DropListReference) AlpcpDereferenceView(View);
}

VOID
NTAPI
AlpcpDereferenceSecurityData(
    _In_ PKALPC_SECURITY_DATA Data)
{
    if (InterlockedDecrement(&Data->ReferenceCount) != 0) return;
    if (Data->ClientContext.ClientToken) SeDeleteClientSecurity(&Data->ClientContext);
    ExFreePoolWithTag(Data, 'XcpA');
}

static
VOID
AlpcpFreeHandleData(
    _In_ PKALPC_HANDLE_DATA HandleData)
{
    ULONG i;

    for (i = 0; i < HandleData->Count; i++)
    {
        if (HandleData->Entries[i].Object) ObDereferenceObject(HandleData->Entries[i].Object);
    }
    ExFreePoolWithTag(HandleData, 'HcpA');
}

static
ULONG
AlpcpObjectTypeMask(
    _In_ PVOID Object)
{
    POBJECT_TYPE Type = ObGetObjectType(Object);

    if (Type == IoFileObjectType) return ALPC_PORFLG_OBJECT_TYPE_FILE;
    if (Type == PsThreadType) return ALPC_PORFLG_OBJECT_TYPE_THREAD;
    if (Type == ExSemaphoreObjectType) return ALPC_PORFLG_OBJECT_TYPE_SEMAPHORE;
    if (Type == ExEventObjectType) return ALPC_PORFLG_OBJECT_TYPE_EVENT;
    if (Type == PsProcessType) return ALPC_PORFLG_OBJECT_TYPE_PROCESS;
    if (Type == ExMutantObjectType) return ALPC_PORFLG_OBJECT_TYPE_MUTEX;
    if (Type == MmSectionObjectType) return ALPC_PORFLG_OBJECT_TYPE_SECTION;
    if (Type == CmKeyObjectType) return ALPC_PORFLG_OBJECT_TYPE_REGKEY;
    if (Type == SeTokenObjectType) return ALPC_PORFLG_OBJECT_TYPE_TOKEN;
    if (Type == PsJobType) return ALPC_PORFLG_OBJECT_TYPE_JOB;
    return ALPC_PORFLG_OBJECT_TYPE_INVALID;
}

VOID
NTAPI
AlpcpReleaseMessageAttributes(
    _In_ PKALPC_MESSAGE Message)
{
    PKALPC_MESSAGE_ATTRIBUTES Attributes = &Message->Attributes;
    PKALPC_VIEW View = Attributes->View;
    ULONG ViewFlags = Attributes->ViewFlags;
    PKALPC_SECURITY_DATA SecurityData = Attributes->SecurityData;
    PKALPC_HANDLE_DATA HandleData = Attributes->HandleData;
    ULONG_PTR DirectEvent = Attributes->DirectEvent;

    Attributes->View = NULL;
    Attributes->ViewFlags = 0;
    Attributes->SecurityData = NULL;
    Attributes->HandleData = NULL;
    Attributes->DirectEvent = 0;
    Attributes->ValidAttributes = 0;

    if (View && (ViewFlags & ALPC_VIEWFLG_AUTO_RELEASE)) AlpcpDeleteView(View);
    if (View) AlpcpDereferenceView(View);
    if (SecurityData) AlpcpDereferenceSecurityData(SecurityData);
    if (HandleData) AlpcpFreeHandleData(HandleData);
    if (DirectEvent & 2)
    {
        ObDereferenceObject((PVOID)(DirectEvent & ~(ULONG_PTR)3));
    }
}

static
NTSTATUS
AlpcpCreateViewInternal(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_SECTION Section,
    _In_ ULONG Flags,
    _Inout_ PSIZE_T ViewSize,
    _Out_ PKALPC_VIEW *OutView)
{
    NTSTATUS Status;
    PKALPC_VIEW View;
    LARGE_INTEGER Offset;
    PVOID Base = NULL;
    SIZE_T Size = *ViewSize;

    if (!Size || (Size > Section->Size)) Size = Section->Size;

    View = ExAllocatePoolWithTag(PagedPool, sizeof(*View), 'VcpA');
    if (!View) return STATUS_NO_MEMORY;
    RtlZeroMemory(View, sizeof(*View));

    Offset.QuadPart = 0;
    Status = MmMapViewOfSection(Section->SectionObject, PsGetCurrentProcess(), &Base, 0, 0, &Offset, &Size, ViewUnmap, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(View, 'VcpA');
        return Status;
    }

    InitializeListHead(&View->Entry);
    View->Flags = Flags;
    View->Section = Section;
    InterlockedIncrement(&Section->ReferenceCount);
    InterlockedIncrement(&Section->ViewCount);
    View->OwnerPort = Port;
    View->Process = PsGetCurrentProcess();
    ObReferenceObject(View->Process);
    View->Address = Base;
    View->Size = Size;
    View->ReferenceCount = 1;
    View->Secure = !(Flags & ALPC_VIEWFLG_NOT_SECURE);

    AlpcpAcquireLock();
    InsertTailList(&Port->ViewList, &View->Entry);
    AlpcpReleaseLock();

    *ViewSize = Size;
    *OutView = View;
    return STATUS_SUCCESS;
}

static
VOID
AlpcpUnlinkView(
    _In_ PKALPC_VIEW View)
{
    AlpcpAcquireLock();
    if (!IsListEmpty(&View->Entry))
    {
        RemoveEntryList(&View->Entry);
        InitializeListHead(&View->Entry);
    }
    AlpcpReleaseLock();
    if (View->Section) InterlockedDecrement(&View->Section->ViewCount);
}

VOID
NTAPI
AlpcpRundownResources(
    _In_ PALPC_PORT Port)
{
    PLIST_ENTRY Entry;
    PKALPC_VIEW View;
    PKALPC_SECTION Section;
    PKALPC_SECURITY_DATA Data;
    PKALPC_RESERVE Reserve;

    for (;;)
    {
        AlpcpAcquireLock();
        if (IsListEmpty(&Port->ViewList))
        {
            AlpcpReleaseLock();
            break;
        }
        Entry = RemoveHeadList(&Port->ViewList);
        InitializeListHead(Entry);
        AlpcpReleaseLock();
        View = CONTAINING_RECORD(Entry, KALPC_VIEW, Entry);
        if (View->Section) InterlockedDecrement(&View->Section->ViewCount);
        AlpcpDereferenceView(View);
    }

    for (;;)
    {
        AlpcpAcquireLock();
        if (IsListEmpty(&Port->SectionList))
        {
            AlpcpReleaseLock();
            break;
        }
        Entry = RemoveHeadList(&Port->SectionList);
        InitializeListHead(Entry);
        AlpcpReleaseLock();
        Section = CONTAINING_RECORD(Entry, KALPC_SECTION, Entry);
        AlpcpDereferenceSection(Section);
    }

    for (;;)
    {
        AlpcpAcquireLock();
        if (IsListEmpty(&Port->SecurityList))
        {
            AlpcpReleaseLock();
            break;
        }
        Entry = RemoveHeadList(&Port->SecurityList);
        InitializeListHead(Entry);
        AlpcpReleaseLock();
        Data = CONTAINING_RECORD(Entry, KALPC_SECURITY_DATA, Entry);
        AlpcpDereferenceSecurityData(Data);
    }

    for (;;)
    {
        AlpcpAcquireLock();
        if (IsListEmpty(&Port->ReserveList))
        {
            AlpcpReleaseLock();
            break;
        }
        Entry = RemoveHeadList(&Port->ReserveList);
        InitializeListHead(Entry);
        AlpcpReleaseLock();
        Reserve = CONTAINING_RECORD(Entry, KALPC_RESERVE, Entry);
        if (Reserve->Message) AlpcpFreeMessage(Reserve->Message);
        ExFreePoolWithTag(Reserve, 'RsvA');
    }
}

static
NTSTATUS
AlpcpQueryTokenLuids(
    _In_ PEPROCESS Process,
    _Out_ PLUID TokenId,
    _Out_ PLUID AuthenticationId,
    _Out_ PLUID ModifiedId)
{
    NTSTATUS Status;
    PTOKEN Token;
    PTOKEN_STATISTICS Statistics;

    Token = PsReferencePrimaryToken(Process);
    Status = SeQueryInformationToken(Token, TokenStatistics, (PVOID*)&Statistics);
    PsDereferencePrimaryToken(Token);
    if (!NT_SUCCESS(Status)) return Status;

    *TokenId = Statistics->TokenId;
    *AuthenticationId = Statistics->AuthenticationId;
    *ModifiedId = Statistics->ModifiedId;
    ExFreePoolWithTag(Statistics, TAG_SE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AlpcpCaptureSendAttributes(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES Attributes,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Valid;
    PALPC_DATA_VIEW_ATTR ViewAttr;
    PALPC_SECURITY_ATTR SecurityAttr;
    PALPC_CONTEXT_ATTR ContextAttr;
    PALPC_HANDLE_ATTR HandleAttr;
    PALPC_DIRECT_ATTR DirectAttr;
    PKALPC_VIEW View;
    PKALPC_SECTION Section;
    PKALPC_SECURITY_DATA Data;
    PKALPC_HANDLE_DATA HandleData;
    PALPC_HANDLE_ATTR32 HandleInformation = NULL;
    ULONG HandleCount = 1, HandleIndex;
    SIZE_T HandleDataSize;
    PVOID Object;
    OBJECT_HANDLE_INFORMATION ObjectHandleInformation;
    PKEVENT DirectEvent;

    if (!Attributes) return STATUS_SUCCESS;
    Valid = Attributes->ValidAttributes;

    if (Valid & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
    {
        ContextAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
        if (AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER)
            Message->Attributes.ServerContext = ContextAttr->MessageContext;
        else
            Message->Attributes.ClientContext = ContextAttr->MessageContext;
        Message->Attributes.ValidAttributes |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    }

    if (Valid & ALPC_MESSAGE_VIEW_ATTRIBUTE)
    {
        ViewAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_VIEW_ATTRIBUTE);
        if (ViewAttr->Flags & ~ALPC_VIEWFLG_VALID) return STATUS_INVALID_PARAMETER;
        if (!ViewAttr->SectionHandle) goto SkipViewAttribute;
        if (!ViewAttr->ViewBase) return STATUS_INVALID_ADDRESS;
        AlpcpAcquireLock();
        Section = AlpcpLookupSection(Port, (ULONG)(ULONG_PTR)ViewAttr->SectionHandle);
        if (!Section)
        {
            AlpcpReleaseLock();
            return STATUS_INVALID_HANDLE;
        }
        View = AlpcpLookupView(Port, ViewAttr->ViewBase);
        if (View && View->Section == Section) InterlockedIncrement(&View->ReferenceCount);
        else View = NULL;
        AlpcpReleaseLock();
        if (!View) return STATUS_INVALID_ADDRESS;
        Message->Attributes.View = View;
        Message->Attributes.ViewFlags = ViewAttr->Flags;
        Message->Attributes.ValidAttributes |= ALPC_MESSAGE_VIEW_ATTRIBUTE;
SkipViewAttribute:;
    }

    if (Valid & ALPC_MESSAGE_SECURITY_ATTRIBUTE)
    {
        SecurityAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_SECURITY_ATTRIBUTE);
        AlpcpAcquireLock();
        Data = AlpcpLookupSecurityData(Port, (ULONG)(ULONG_PTR)SecurityAttr->ContextHandle);
        if (Data) InterlockedIncrement(&Data->ReferenceCount);
        AlpcpReleaseLock();
        if (!Data) return STATUS_INVALID_PARAMETER;
        Message->Attributes.SecurityData = Data;
        Message->Attributes.ValidAttributes |= ALPC_MESSAGE_SECURITY_ATTRIBUTE;
    }

    if (Valid & ALPC_MESSAGE_HANDLE_ATTRIBUTE)
    {
        HandleAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
        if (HandleAttr->Flags & ~(ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS |
                                  ALPC_HANDLEFLG_DUPLICATE_SAME_ATTRIBUTES |
                                  ALPC_HANDLEFLG_INDIRECT |
                                  ALPC_HANDLEFLG_DUPLICATE_INHERIT))
            return STATUS_INVALID_PARAMETER;

        if (HandleAttr->Flags & ALPC_HANDLEFLG_INDIRECT)
        {
            if (!(Port->PortAttributes.Flags & ALPC_PORFLG_ALLOW_MULTIHANDLE_ATTRIBUTE))
                return STATUS_INVALID_PARAMETER;
            HandleCount = HandleAttr->HandleCount;
            if (HandleCount > 512)
                return STATUS_LPC_HANDLE_COUNT_EXCEEDED;
            if (HandleCount < 2 || !HandleAttr->HandleAttrArray)
                return STATUS_INVALID_PARAMETER;
            HandleInformation = ExAllocatePoolWithTag(PagedPool, HandleCount * sizeof(*HandleInformation), 'IcpA');
            if (!HandleInformation) return STATUS_NO_MEMORY;
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(HandleAttr->HandleAttrArray,
                                 HandleCount * sizeof(*HandleInformation),
                                 sizeof(ULONG));
                    RtlCopyMemory(HandleInformation,
                                  HandleAttr->HandleAttrArray,
                                  HandleCount * sizeof(*HandleInformation));
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status))
                {
                    ExFreePoolWithTag(HandleInformation, 'IcpA');
                    return Status;
                }
            }
            else
            {
                RtlCopyMemory(HandleInformation,
                              HandleAttr->HandleAttrArray,
                              HandleCount * sizeof(*HandleInformation));
            }
        }

        HandleDataSize = FIELD_OFFSET(KALPC_HANDLE_DATA, Entries) +
                         HandleCount * sizeof(HandleData->Entries[0]);
        HandleData = ExAllocatePoolWithTag(PagedPool, HandleDataSize, 'HcpA');
        if (!HandleData)
        {
            if (HandleInformation) ExFreePoolWithTag(HandleInformation, 'IcpA');
            return STATUS_NO_MEMORY;
        }
        RtlZeroMemory(HandleData, HandleDataSize);
        HandleData->Flags = HandleAttr->Flags;
        HandleData->Count = HandleCount;

        for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++)
        {
            HANDLE SourceHandle;
            ACCESS_MASK DesiredAccess;
            ULONG ObjectType, EntryFlags, Index;

            if (HandleInformation)
            {
                SourceHandle = UlongToHandle(HandleInformation[HandleIndex].Handle);
                DesiredAccess = HandleInformation[HandleIndex].DesiredAccess;
                ObjectType = HandleInformation[HandleIndex].ObjectType;
                EntryFlags = HandleInformation[HandleIndex].Flags;
                Index = HandleIndex;
            }
            else
            {
                SourceHandle = HandleAttr->Handle;
                DesiredAccess = HandleAttr->DesiredAccess;
                ObjectType = HandleAttr->ObjectType;
                EntryFlags = HandleAttr->Flags;
                Index = 0;
            }

            if (EntryFlags & ~(ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS |
                               ALPC_HANDLEFLG_DUPLICATE_SAME_ATTRIBUTES |
                               ALPC_HANDLEFLG_DUPLICATE_INHERIT))
            {
                Status = STATUS_INVALID_PARAMETER;
                HandleData->Count = HandleIndex;
                AlpcpFreeHandleData(HandleData);
                if (HandleInformation) ExFreePoolWithTag(HandleInformation, 'IcpA');
                return Status;
            }

            Status = ObReferenceObjectByHandle(SourceHandle, (EntryFlags & ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS) ? 0 : DesiredAccess, NULL, PreviousMode, &Object, &ObjectHandleInformation);
            if (!NT_SUCCESS(Status))
            {
                HandleData->Count = HandleIndex;
                AlpcpFreeHandleData(HandleData);
                if (HandleInformation) ExFreePoolWithTag(HandleInformation, 'IcpA');
                return Status;
            }
            ObjectType = AlpcpObjectTypeMask(Object);
            if (!(ObjectType & ALPC_PORFLG_OBJECT_TYPE_ALL_OBJECTS))
            {
                ObDereferenceObject(Object);
                HandleData->Count = HandleIndex;
                AlpcpFreeHandleData(HandleData);
                if (HandleInformation) ExFreePoolWithTag(HandleInformation, 'IcpA');
                return STATUS_ACCESS_DENIED;
            }

            HandleData->Entries[HandleIndex].Object = Object;
            HandleData->Entries[HandleIndex].Index = Index;
            HandleData->Entries[HandleIndex].ObjectType = ObjectType;
            HandleData->Entries[HandleIndex].DesiredAccess =
                (EntryFlags & ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS) ?
                    ObjectHandleInformation.GrantedAccess : DesiredAccess;
            HandleData->Entries[HandleIndex].Flags = EntryFlags;
            HandleData->Entries[HandleIndex].HandleAttributes =
                ObjectHandleInformation.HandleAttributes;
        }
        if (HandleInformation) ExFreePoolWithTag(HandleInformation, 'IcpA');
        Message->Attributes.HandleData = HandleData;
        Message->Attributes.ValidAttributes |= ALPC_MESSAGE_HANDLE_ATTRIBUTE;
    }

    if (Valid & ALPC_MESSAGE_TOKEN_ATTRIBUTE)
    {
        Status = AlpcpQueryTokenLuids(PsGetCurrentProcess(), &Message->Attributes.TokenId, &Message->Attributes.AuthenticationId, &Message->Attributes.ModifiedId);
        if (!NT_SUCCESS(Status)) return Status;
        Message->Attributes.ValidAttributes |= ALPC_MESSAGE_TOKEN_ATTRIBUTE;
    }

    if (Valid & ALPC_MESSAGE_DIRECT_ATTRIBUTE)
    {
        DirectAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_DIRECT_ATTRIBUTE);
        if (PreviousMode == KernelMode)
        {
            Message->Attributes.DirectEvent =
                (ULONG_PTR)DirectAttr->Event | 1;
        }
        else
        {
            Status = ObReferenceObjectByHandle(DirectAttr->Event, EVENT_MODIFY_STATE, ExEventObjectType, PreviousMode, (PVOID *)&DirectEvent, NULL);
            if (!NT_SUCCESS(Status)) return Status;
            Message->Attributes.DirectEvent = (ULONG_PTR)DirectEvent | 2;
        }
        Message->Attributes.ValidAttributes |= ALPC_MESSAGE_DIRECT_ATTRIBUTE;
    }

    if (Valid & ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE)
    {
        /* This value is kernel-derived; the caller-supplied ticket is never trusted. */
        Message->Attributes.WorkOnBehalfTicket = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AlpcpExposeReceiveAttributes(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES Attributes,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES UserAttributes,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Wanted, Exposed = 0;
    PALPC_DATA_VIEW_ATTR ViewAttr;
    PALPC_SECURITY_ATTR SecurityAttr;
    PALPC_CONTEXT_ATTR ContextAttr;
    PALPC_HANDLE_ATTR HandleAttr;
    PALPC_TOKEN_ATTR TokenAttr;
    PALPC_DIRECT_ATTR DirectAttr;
    PALPC_WORK_ON_BEHALF_ATTR WorkAttr;
    PKALPC_VIEW View, SourceView;
    PKALPC_HANDLE_DATA HandleData;
    PALPC_MESSAGE_HANDLE_INFORMATION HandleInformation = NULL;
    PALPC_MESSAGE_HANDLE_INFORMATION UserHandleInformation;
    ULONG HandleCapacity, HandleIndex;
    SIZE_T ViewSize;
    HANDLE NewHandle, SingleHandle = NULL;
    ULONG OpenedHandleCount = 0;

    if (!Attributes) return STATUS_SUCCESS;
    Wanted = Attributes->AllocatedAttributes;

    if (Wanted & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
    {
        ContextAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
        ContextAttr->PortContext = Message->PortContext;
        ContextAttr->MessageContext = AlpcpMessageContextForPort(Port, Message);
        ContextAttr->Sequence = Message->Sequence;
        ContextAttr->MessageId = Message->PortMessage.MessageId;
        ContextAttr->CallbackId = Message->PortMessage.CallbackId;
        Exposed |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    }

    if ((Wanted & ALPC_MESSAGE_VIEW_ATTRIBUTE) &&
        (Message->Attributes.ValidAttributes & ALPC_MESSAGE_VIEW_ATTRIBUTE))
    {
        ViewAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_VIEW_ATTRIBUTE);
        SourceView = Message->Attributes.View;
        if (SourceView && SourceView->Section)
        {
            if (SourceView->Process == PsGetCurrentProcess())
            {
                ViewAttr->Flags = Message->Attributes.ViewFlags;
                ViewAttr->SectionHandle = (ALPC_HANDLE)(ULONG_PTR)SourceView->Section->Handle;
                ViewAttr->ViewBase = SourceView->Address;
                ViewAttr->ViewSize = SourceView->Size;
                Exposed |= ALPC_MESSAGE_VIEW_ATTRIBUTE;
            }
            else
            {
                AlpcpAcquireLock();
                View = AlpcpFindViewBySection(Port, SourceView->Section);
                AlpcpReleaseLock();
                Status = STATUS_SUCCESS;
                if (!View)
                {
                    ViewSize = SourceView->Size;
                    Status = AlpcpCreateViewInternal(Port, SourceView->Section, Message->Attributes.ViewFlags & ~ALPC_VIEWFLG_UNMAP_EXISTING, &ViewSize, &View);
                }
                if (NT_SUCCESS(Status))
                {
                    ViewAttr->Flags = Message->Attributes.ViewFlags;
                    ViewAttr->SectionHandle = (ALPC_HANDLE)(ULONG_PTR)SourceView->Section->Handle;
                    ViewAttr->ViewBase = View->Address;
                    ViewAttr->ViewSize = View->Size;
                    Exposed |= ALPC_MESSAGE_VIEW_ATTRIBUTE;
                }
            }
        }
    }

    if ((Wanted & ALPC_MESSAGE_SECURITY_ATTRIBUTE) &&
        (Message->Attributes.ValidAttributes & ALPC_MESSAGE_SECURITY_ATTRIBUTE))
    {
        SecurityAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_SECURITY_ATTRIBUTE);
        SecurityAttr->Flags = Message->Attributes.SecurityData->Flags;
        SecurityAttr->QoS = NULL;
        SecurityAttr->ContextHandle = (ALPC_HANDLE)(ULONG_PTR)Message->Attributes.SecurityData->Handle;
        Exposed |= ALPC_MESSAGE_SECURITY_ATTRIBUTE;
    }

    if ((Wanted & ALPC_MESSAGE_HANDLE_ATTRIBUTE) &&
        (Message->Attributes.ValidAttributes & ALPC_MESSAGE_HANDLE_ATTRIBUTE))
    {
        HandleAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
        HandleData = Message->Attributes.HandleData;
        if (HandleData && HandleData->Count && HandleData->Entries[0].Object)
        {
            if (!(Port->PortAttributes.Flags & ALPC_PORFLG_ALLOW_DUP_OBJECT))
                return STATUS_ACCESS_DENIED;

            for (HandleIndex = 0; HandleIndex < HandleData->Count; HandleIndex++)
            {
                if (!(Port->PortAttributes.DupObjectTypes &
                      HandleData->Entries[HandleIndex].ObjectType))
                {
                    return STATUS_OBJECT_TYPE_MISMATCH;
                }
            }

            if ((HandleData->Flags & ALPC_HANDLEFLG_INDIRECT) || HandleData->Count > 1)
            {
                UserHandleInformation = HandleAttr->HandleAttrArray;
                HandleCapacity = HandleAttr->HandleCount;
                HandleAttr->Flags = HandleData->Flags | ALPC_HANDLEFLG_INDIRECT;
                HandleAttr->HandleCount = HandleData->Count;
                if (!UserHandleInformation || HandleCapacity < HandleData->Count)
                    return STATUS_BUFFER_TOO_SMALL;

                HandleInformation = ExAllocatePoolWithTag(PagedPool, HandleData->Count * sizeof(*HandleInformation), 'IcpA');
                if (!HandleInformation) return STATUS_NO_MEMORY;
                RtlZeroMemory(HandleInformation,
                              HandleData->Count * sizeof(*HandleInformation));

                for (HandleIndex = 0; HandleIndex < HandleData->Count; HandleIndex++)
                {
                    Status = ObOpenObjectByPointer(HandleData->Entries[HandleIndex].Object, ((HandleData->Entries[HandleIndex].Flags & ALPC_HANDLEFLG_DUPLICATE_SAME_ATTRIBUTES) ? HandleData->Entries[HandleIndex].HandleAttributes : 0) | ((HandleData->Entries[HandleIndex].Flags & ALPC_HANDLEFLG_DUPLICATE_INHERIT) ? OBJ_INHERIT : 0), NULL, HandleData->Entries[HandleIndex].DesiredAccess, NULL, PreviousMode, &NewHandle);
                    if (!NT_SUCCESS(Status)) break;
                    OpenedHandleCount++;
                    HandleInformation[HandleIndex].Index =
                        HandleData->Entries[HandleIndex].Index;
                    HandleInformation[HandleIndex].Flags =
                        HandleData->Entries[HandleIndex].Flags;
                    HandleInformation[HandleIndex].Handle = HandleToUlong(NewHandle);
                    HandleInformation[HandleIndex].ObjectType =
                        HandleData->Entries[HandleIndex].ObjectType;
                    HandleInformation[HandleIndex].GrantedAccess =
                        HandleData->Entries[HandleIndex].DesiredAccess;
                }

                if (NT_SUCCESS(Status))
                {
                    if (PreviousMode != KernelMode)
                    {
                        _SEH2_TRY
                        {
                            ProbeForWrite(UserHandleInformation,
                                          HandleData->Count * sizeof(*HandleInformation),
                                          sizeof(ULONG));
                            RtlCopyMemory(UserHandleInformation,
                                          HandleInformation,
                                          HandleData->Count * sizeof(*HandleInformation));
                        }
                        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                        {
                            Status = _SEH2_GetExceptionCode();
                        }
                        _SEH2_END;
                    }
                    else
                    {
                        RtlCopyMemory(UserHandleInformation,
                                      HandleInformation,
                                      HandleData->Count * sizeof(*HandleInformation));
                    }
                }

                if (!NT_SUCCESS(Status))
                {
                    goto Exit;
                }
                Exposed |= ALPC_MESSAGE_HANDLE_ATTRIBUTE;
            }
            else
            {
                Status = ObOpenObjectByPointer(HandleData->Entries[0].Object, ((HandleData->Entries[0].Flags & ALPC_HANDLEFLG_DUPLICATE_SAME_ATTRIBUTES) ? HandleData->Entries[0].HandleAttributes : 0) | ((HandleData->Entries[0].Flags & ALPC_HANDLEFLG_DUPLICATE_INHERIT) ? OBJ_INHERIT : 0), NULL, HandleData->Entries[0].DesiredAccess, NULL, PreviousMode, &NewHandle);
                if (!NT_SUCCESS(Status)) return Status;
                SingleHandle = NewHandle;
                HandleAttr->Flags = HandleData->Entries[0].Flags;
                HandleAttr->Handle = NewHandle;
                HandleAttr->ObjectType = HandleData->Entries[0].ObjectType;
                HandleAttr->DesiredAccess = HandleData->Entries[0].DesiredAccess;
                Exposed |= ALPC_MESSAGE_HANDLE_ATTRIBUTE;
            }
        }
    }

    if (Wanted & ALPC_MESSAGE_TOKEN_ATTRIBUTE)
    {
        TokenAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_TOKEN_ATTRIBUTE);
        if (!(Message->Attributes.ValidAttributes & ALPC_MESSAGE_TOKEN_ATTRIBUTE))
        {
            PEPROCESS SenderProcess;
            PETHREAD SenderThread;

            if (NT_SUCCESS(PsLookupProcessThreadByCid(&Message->PortMessage.ClientId, &SenderProcess, &SenderThread)))
            {
                if (NT_SUCCESS(AlpcpQueryTokenLuids(SenderProcess, &Message->Attributes.TokenId, &Message->Attributes.AuthenticationId, &Message->Attributes.ModifiedId)))
                {
                    Message->Attributes.ValidAttributes |= ALPC_MESSAGE_TOKEN_ATTRIBUTE;
                }
                ObDereferenceObject(SenderThread);
                ObDereferenceObject(SenderProcess);
            }
        }
        if (Message->Attributes.ValidAttributes & ALPC_MESSAGE_TOKEN_ATTRIBUTE)
        {
            TokenAttr->TokenId = *(PULONGLONG)&Message->Attributes.TokenId;
            TokenAttr->AuthenticationId = *(PULONGLONG)&Message->Attributes.AuthenticationId;
            TokenAttr->ModifiedId = *(PULONGLONG)&Message->Attributes.ModifiedId;
            Exposed |= ALPC_MESSAGE_TOKEN_ATTRIBUTE;
        }
    }

    if (Wanted & ALPC_MESSAGE_DIRECT_ATTRIBUTE)
    {
        DirectAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_DIRECT_ATTRIBUTE);
        DirectAttr->Event = NULL;
    }

    if (Wanted & ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE)
    {
        WorkAttr = AlpcpAttributePointer(Attributes, ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE);
        WorkAttr->Ticket = Message->Attributes.WorkOnBehalfTicket;
        if (WorkAttr->Ticket)
        {
            Exposed |= ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE;
        }
    }

    Attributes->ValidAttributes = Exposed;
    Status = AlpcpWriteAttributes(UserAttributes, Attributes, PreviousMode);

Exit:
    if (!NT_SUCCESS(Status))
    {
        while (OpenedHandleCount)
        {
            OpenedHandleCount--;
            ObCloseHandle(UlongToHandle(HandleInformation[OpenedHandleCount].Handle),
                          KernelMode);
        }
        if (SingleHandle) ObCloseHandle(SingleHandle, KernelMode);
    }
    if (HandleInformation) ExFreePoolWithTag(HandleInformation, 'IcpA');
    return Status;
}

NTSTATUS
NTAPI
AlpcpImpersonateMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_ ULONG Flags)
{
    NTSTATUS Status;
    PKALPC_SECURITY_DATA Data = NULL;
    PALPC_PORT ClientPort = NULL;
    PETHREAD ClientThread;
    SECURITY_CLIENT_CONTEXT ClientContext;
    SECURITY_QUALITY_OF_SERVICE Qos;
    BOOLEAN Dynamic = TRUE;
    BOOLEAN AnonymousFallback;
    BOOLEAN RequireLevel;
    SECURITY_IMPERSONATION_LEVEL RequiredLevel;

    if (Flags > 0xF) return STATUS_INVALID_PARAMETER;

    AnonymousFallback = (Flags & ALPC_IMPERSONATEFLG_ANONYMOUS_FALLBACK) != 0;
    RequiredLevel = (SECURITY_IMPERSONATION_LEVEL)(Flags >> 2);
    RequireLevel = (Flags & (ALPC_IMPERSONATEFLG_REQUIRE_IMPERSONATION_LEVEL |
                             ((ULONG)RequiredLevel << 2))) != 0;

    AlpcpAcquireLock();
    if (Message->Attributes.SecurityData)
    {
        Data = Message->Attributes.SecurityData;
        InterlockedIncrement(&Data->ReferenceCount);
    }
    else if (Port->CommunicationInfo && Port->CommunicationInfo->ClientCommunicationPort &&
             ObReferenceObjectSafe(Port->CommunicationInfo->ClientCommunicationPort))
    {
        ClientPort = Port->CommunicationInfo->ClientCommunicationPort;
        Dynamic = (ClientPort->Flags & ALPC_PORT_FLAG_DYNAMIC_SECURITY) != 0;
        Qos = ClientPort->SecurityQos;
    }
    AlpcpReleaseLock();

    if (Data)
    {
        if (Data->Revoked)
        {
            Status = STATUS_REVISION_MISMATCH;
        }
        else if (RequireLevel &&
                 Data->ClientContext.SecurityQos.ImpersonationLevel < RequiredLevel)
        {
            Status = STATUS_ACCESS_DENIED;
        }
        else
        {
            Status = SeImpersonateClientEx(&Data->ClientContext, NULL);
        }
        AlpcpDereferenceSecurityData(Data);
        if (!NT_SUCCESS(Status) && AnonymousFallback)
        {
            Status = PsImpersonateClient(PsGetCurrentThread(), SeAnonymousLogonToken, FALSE, TRUE, SecurityAnonymous);
        }
        return Status;
    }

    if (!ClientPort)
    {
        if (AnonymousFallback)
        {
            return PsImpersonateClient(PsGetCurrentThread(), SeAnonymousLogonToken, FALSE, TRUE, SecurityAnonymous);
        }
        return STATUS_PORT_DISCONNECTED;
    }

    if (RequireLevel && Qos.ImpersonationLevel < RequiredLevel)
    {
        ObDereferenceObject(ClientPort);
        return STATUS_ACCESS_DENIED;
    }

    if (!Dynamic)
    {
        Status = SeImpersonateClientEx(&ClientPort->StaticSecurity, NULL);
    }
    else
    {
        Status = PsLookupProcessThreadByCid(&Message->PortMessage.ClientId, NULL, &ClientThread);
        if (NT_SUCCESS(Status))
        {
            Status = SeCreateClientSecurity(ClientThread, &Qos, FALSE, &ClientContext);
            if (NT_SUCCESS(Status))
            {
                Status = SeImpersonateClientEx(&ClientContext, NULL);
                SeDeleteClientSecurity(&ClientContext);
            }
            ObDereferenceObject(ClientThread);
        }
    }
    ObDereferenceObject(ClientPort);

    if (!NT_SUCCESS(Status) && AnonymousFallback)
    {
        Status = PsImpersonateClient(PsGetCurrentThread(), SeAnonymousLogonToken, FALSE, TRUE, SecurityAnonymous);
    }
    return Status;
}

NTSTATUS
NTAPI
NtAlpcCreatePortSection(
    _In_ HANDLE PortHandle,
    _In_ ULONG Flags,
    _In_opt_ HANDLE SectionHandle,
    _In_ SIZE_T SectionSize,
    _Out_ PALPC_HANDLE AlpcSectionHandle,
    _Out_ PSIZE_T ActualSectionSize)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PKALPC_SECTION Section;
    PVOID SectionObject;
    LARGE_INTEGER MaximumSize;
    ALPC_HANDLE Handle;

    PAGED_CODE();

    if ((Flags & ~ALPC_PORTSECTIONFLG_SECURE) ||
        ((Flags & ALPC_PORTSECTIONFLG_SECURE) && SectionHandle) ||
        !SectionSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWritePointer(AlpcSectionHandle);
            ProbeForWrite(ActualSectionSize, sizeof(SIZE_T), sizeof(SIZE_T));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    if (SectionHandle)
    {
        Status = ObReferenceObjectByHandle(SectionHandle, SECTION_MAP_READ | SECTION_MAP_WRITE, MmSectionObjectType, PreviousMode, &SectionObject, NULL);
    }
    else
    {
        MaximumSize.QuadPart = SectionSize;
        Status = MmCreateSection(&SectionObject, SECTION_ALL_ACCESS, NULL, &MaximumSize, PAGE_READWRITE, SEC_COMMIT, NULL, NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    Section = ExAllocatePoolWithTag(PagedPool, sizeof(*Section), 'ScpA');
    if (!Section)
    {
        ObDereferenceObject(SectionObject);
        ObDereferenceObject(Port);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Section, sizeof(*Section));
    InitializeListHead(&Section->Entry);
    Section->Flags = Flags;
    Section->SectionObject = SectionObject;
    Section->Size = ROUND_TO_PAGES(SectionSize);
    Section->OwnerPort = Port;
    Section->ReferenceCount = 1;

    AlpcpAcquireLock();
    Section->Handle = Port->NextResourceHandle++;
    InsertTailList(&Port->SectionList, &Section->Entry);
    AlpcpReleaseLock();

    Handle = (ALPC_HANDLE)(ULONG_PTR)Section->Handle;
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *AlpcSectionHandle = Handle;
            *ActualSectionSize = Section->Size;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *AlpcSectionHandle = Handle;
        *ActualSectionSize = Section->Size;
    }

    if (!NT_SUCCESS(Status))
    {
        AlpcpAcquireLock();
        if (!IsListEmpty(&Section->Entry))
        {
            RemoveEntryList(&Section->Entry);
            InitializeListHead(&Section->Entry);
        }
        AlpcpReleaseLock();
        AlpcpDereferenceSection(Section);
    }

    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcDeletePortSection(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _In_ ALPC_HANDLE SectionHandle)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PKALPC_SECTION Section;
    BOOLEAN DeletePending = FALSE;

    PAGED_CODE();

    if (Flags) return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    Section = AlpcpLookupSection(Port, (ULONG)(ULONG_PTR)SectionHandle);
    if (Section && (Section->ReferenceCount == 1) && !Section->ViewCount)
    {
        RemoveEntryList(&Section->Entry);
        InitializeListHead(&Section->Entry);
    }
    else if (Section)
    {
        DeletePending = TRUE;
    }
    AlpcpReleaseLock();

    if (Section && !DeletePending) AlpcpDereferenceSection(Section);
    ObDereferenceObject(Port);
    if (!Section) return STATUS_INVALID_HANDLE;
    return DeletePending ? STATUS_DELETE_PENDING : STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAlpcCreateSectionView(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _Inout_ PALPC_DATA_VIEW_ATTR ViewAttributes)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PKALPC_SECTION Section;
    PKALPC_VIEW View;
    ALPC_DATA_VIEW_ATTR Captured;
    SIZE_T ViewSize;

    PAGED_CODE();

    if (Flags) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(ViewAttributes, sizeof(*ViewAttributes), sizeof(ULONG));
            Captured = *(volatile ALPC_DATA_VIEW_ATTR*)ViewAttributes;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Captured = *ViewAttributes;
    }

    if (Captured.Flags || Captured.ViewBase || !Captured.ViewSize)
        return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    Section = AlpcpLookupSection(Port, (ULONG)(ULONG_PTR)Captured.SectionHandle);
    if (Section) InterlockedIncrement(&Section->ReferenceCount);
    AlpcpReleaseLock();
    if (!Section)
    {
        ObDereferenceObject(Port);
        return STATUS_INVALID_HANDLE;
    }

    ViewSize = Captured.ViewSize;
    Status = AlpcpCreateViewInternal(Port, Section, Captured.Flags, &ViewSize, &View);
    AlpcpDereferenceSection(Section);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ViewAttributes->ViewBase = View->Address;
            ViewAttributes->ViewSize = View->Size;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        ViewAttributes->ViewBase = View->Address;
        ViewAttributes->ViewSize = View->Size;
    }

    if (!NT_SUCCESS(Status))
    {
        AlpcpUnlinkView(View);
        AlpcpDereferenceView(View);
    }

    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcDeleteSectionView(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _In_ PVOID ViewBase)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PKALPC_VIEW View;
    BOOLEAN DeletePending = FALSE;

    PAGED_CODE();

    if (Flags) return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    View = AlpcpLookupView(Port, ViewBase);
    if (View && (View->ReferenceCount == 1))
    {
        RemoveEntryList(&View->Entry);
        InitializeListHead(&View->Entry);
    }
    else if (View)
    {
        View->DeletePending = TRUE;
        DeletePending = TRUE;
    }
    AlpcpReleaseLock();

    if (View && !DeletePending)
    {
        if (View->Section) InterlockedDecrement(&View->Section->ViewCount);
        AlpcpDereferenceView(View);
    }
    ObDereferenceObject(Port);
    if (!View) return STATUS_INVALID_ADDRESS;
    return DeletePending ? STATUS_DELETE_PENDING : STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpCreateSecurityContextForThread(
    _In_ PALPC_PORT Port,
    _In_ PETHREAD Thread,
    _In_ ULONG SecurityFlags,
    _In_ PSECURITY_QUALITY_OF_SERVICE Qos,
    _Out_ PKALPC_SECURITY_DATA *SecurityData)
{
    NTSTATUS Status;
    PKALPC_SECURITY_DATA Data;

    Data = ExAllocatePoolWithTag(PagedPool, sizeof(*Data), 'XcpA');
    if (!Data) return STATUS_NO_MEMORY;

    RtlZeroMemory(Data, sizeof(*Data));
    InitializeListHead(&Data->Entry);
    Data->Flags = SecurityFlags;
    Data->OwnerPort = Port;
    Data->ReferenceCount = 1;

    Status = SeCreateClientSecurity(Thread, Qos, FALSE, &Data->ClientContext);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Data, 'XcpA');
        return Status;
    }

    AlpcpAcquireLock();
    Data->Handle = Port->NextResourceHandle++ + ALPC_SECURITY_CONTEXT_ID_BIAS;
    InsertTailList(&Port->SecurityList, &Data->Entry);
    AlpcpReleaseLock();

    *SecurityData = Data;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAlpcCreateSecurityContext(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _Inout_ PALPC_SECURITY_ATTR SecurityAttribute)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PKALPC_SECURITY_DATA Data;
    ALPC_SECURITY_ATTR Captured;
    SECURITY_QUALITY_OF_SERVICE Qos;

    PAGED_CODE();

    if (Flags) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(SecurityAttribute, sizeof(*SecurityAttribute), sizeof(ULONG));
            Captured = *(volatile ALPC_SECURITY_ATTR*)SecurityAttribute;
            if (Captured.QoS)
            {
                ProbeForRead(Captured.QoS, sizeof(*Captured.QoS), sizeof(ULONG));
                Qos = *(volatile SECURITY_QUALITY_OF_SERVICE*)Captured.QoS;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Captured = *SecurityAttribute;
        if (Captured.QoS) Qos = *Captured.QoS;
    }

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    if (!Captured.QoS) Qos = Port->SecurityQos;

    Status = AlpcpCreateSecurityContextForThread(Port, PsGetCurrentThread(), Captured.Flags, &Qos, &Data);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            SecurityAttribute->ContextHandle = (ALPC_HANDLE)(ULONG_PTR)Data->Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        SecurityAttribute->ContextHandle = (ALPC_HANDLE)(ULONG_PTR)Data->Handle;
    }

    if (!NT_SUCCESS(Status))
    {
        AlpcpAcquireLock();
        if (!IsListEmpty(&Data->Entry))
        {
            RemoveEntryList(&Data->Entry);
            InitializeListHead(&Data->Entry);
        }
        AlpcpReleaseLock();
        AlpcpDereferenceSecurityData(Data);
    }

    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
AlpcCreateSecurityContext(
    _In_ HANDLE PortHandle,
    _In_ PETHREAD Thread,
    _Reserved_ ULONG Flags,
    _Inout_ PALPC_SECURITY_ATTR SecurityAttribute)
{
    NTSTATUS Status;
    PALPC_PORT Port;
    PKALPC_SECURITY_DATA Data;
    PSECURITY_QUALITY_OF_SERVICE Qos;

    PAGED_CODE();

    if (Flags || !Thread || !SecurityAttribute)
        return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, KernelMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Qos = SecurityAttribute->QoS ? SecurityAttribute->QoS : &Port->SecurityQos;
    Status = AlpcpCreateSecurityContextForThread(Port, Thread, SecurityAttribute->Flags, Qos, &Data);
    if (NT_SUCCESS(Status))
        SecurityAttribute->ContextHandle = (ALPC_HANDLE)(ULONG_PTR)Data->Handle;

    ObDereferenceObject(Port);
    return Status;
}

static
NTSTATUS
AlpcpDeleteOrRevokeSecurityContext(
    _In_ HANDLE PortHandle,
    _In_ ALPC_HANDLE ContextHandle,
    _In_ BOOLEAN Revoke)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PKALPC_SECURITY_DATA Data;
    BOOLEAN Conflict = FALSE;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    Data = AlpcpLookupSecurityData(Port, (ULONG)(ULONG_PTR)ContextHandle);
    if (Data)
    {
        if (Revoke && Data->Revoked)
        {
            Conflict = TRUE;
        }
        else if (Revoke)
        {
            Data->Revoked = TRUE;
        }
        else if (Data->ReferenceCount != 1)
        {
            Conflict = TRUE;
        }
        else
        {
            RemoveEntryList(&Data->Entry);
            InitializeListHead(&Data->Entry);
        }
    }
    AlpcpReleaseLock();

    if (Data && !Conflict && !Revoke) AlpcpDereferenceSecurityData(Data);
    ObDereferenceObject(Port);
    if (!Data) return STATUS_INVALID_HANDLE;
    if (Conflict) return Revoke ? STATUS_UNSUCCESSFUL : STATUS_DELETE_PENDING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAlpcDeleteSecurityContext(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _In_ ALPC_HANDLE ContextHandle)
{
    PAGED_CODE();
    if (Flags) return STATUS_INVALID_PARAMETER;
    return AlpcpDeleteOrRevokeSecurityContext(PortHandle, ContextHandle, FALSE);
}

NTSTATUS
NTAPI
NtAlpcRevokeSecurityContext(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _In_ ALPC_HANDLE ContextHandle)
{
    PAGED_CODE();
    if (Flags) return STATUS_INVALID_PARAMETER;
    return AlpcpDeleteOrRevokeSecurityContext(PortHandle, ContextHandle, TRUE);
}

NTSTATUS
NTAPI
NtAlpcCreateResourceReserve(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _In_ SIZE_T MessageSize,
    _Out_ PULONG ResourceId)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PKALPC_RESERVE Reserve;

    PAGED_CODE();

    if (Flags) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(ResourceId, sizeof(*ResourceId), sizeof(UCHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if (MessageSize > ALPC_MAX_RESERVE_MESSAGE_LENGTH) return STATUS_BUFFER_OVERFLOW;
    if (MessageSize < sizeof(PORT_MESSAGE)) return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Reserve = ExAllocatePoolWithTag(PagedPool, sizeof(*Reserve), 'RsvA');
    if (!Reserve)
    {
        ObDereferenceObject(Port);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Reserve, sizeof(*Reserve));
    InitializeListHead(&Reserve->Entry);
    Reserve->Size = MessageSize;
    Reserve->OwnerPort = Port;
    Reserve->Message = AlpcpAllocateMessage((ULONG)MessageSize, Port);
    if (!Reserve->Message)
    {
        ExFreePoolWithTag(Reserve, 'RsvA');
        ObDereferenceObject(Port);
        return STATUS_NO_MEMORY;
    }

    AlpcpAcquireLock();
    Reserve->Handle = Port->NextResourceHandle++;
    InsertTailList(&Port->ReserveList, &Reserve->Entry);
    AlpcpReleaseLock();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ResourceId = Reserve->Handle | ALPC_RESOURCE_RESERVE_ID_BIT;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *ResourceId = Reserve->Handle | ALPC_RESOURCE_RESERVE_ID_BIT;
    }

    if (!NT_SUCCESS(Status))
    {
        AlpcpAcquireLock();
        if (!IsListEmpty(&Reserve->Entry))
        {
            RemoveEntryList(&Reserve->Entry);
            InitializeListHead(&Reserve->Entry);
        }
        AlpcpReleaseLock();
        AlpcpFreeMessage(Reserve->Message);
        ExFreePoolWithTag(Reserve, 'RsvA');
    }

    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcDeleteResourceReserve(
    _In_ HANDLE PortHandle,
    _Reserved_ ULONG Flags,
    _In_ ULONG ResourceId)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PLIST_ENTRY Entry;
    PKALPC_RESERVE Reserve = NULL, Current;
    BOOLEAN DeletePending = FALSE;

    PAGED_CODE();

    if (Flags || !(ResourceId & ALPC_RESOURCE_RESERVE_ID_BIT))
        return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    for (Entry = Port->ReserveList.Flink; Entry != &Port->ReserveList; Entry = Entry->Flink)
    {
        Current = CONTAINING_RECORD(Entry, KALPC_RESERVE, Entry);
        if (Current->Handle == (ResourceId & ~ALPC_RESOURCE_RESERVE_ID_BIT))
        {
            Reserve = Current;
            if (Reserve->Message && (Reserve->Message->State &
                                     (ALPC_MSG_STATE_QUEUED |
                                      ALPC_MSG_STATE_PENDING |
                                      ALPC_MSG_STATE_SYNC)))
            {
                DeletePending = TRUE;
            }
            else
            {
                RemoveEntryList(&Reserve->Entry);
                InitializeListHead(&Reserve->Entry);
            }
            break;
        }
    }
    AlpcpReleaseLock();

    if (Reserve && !DeletePending)
    {
        if (Reserve->Message) AlpcpFreeMessage(Reserve->Message);
        ExFreePoolWithTag(Reserve, 'RsvA');
    }
    ObDereferenceObject(Port);
    if (!Reserve) return STATUS_INVALID_HANDLE;
    return DeletePending ? STATUS_DELETE_PENDING : STATUS_SUCCESS;
}
