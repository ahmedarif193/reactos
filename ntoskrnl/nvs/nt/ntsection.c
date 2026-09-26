/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntsection.c
 * PURPOSE:     NT section and view mapping interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

#define MI_HEADER_BUFFER_SIZE (64 * 1024)

POBJECT_TYPE MmSectionObjectType;

static MI_RWLOCK MiControlLock;
static LONG64 MiBasedSectionCursor;

static GENERIC_MAPPING MiSectionMapping =
{
    STANDARD_RIGHTS_READ | SECTION_MAP_READ | SECTION_QUERY,
    STANDARD_RIGHTS_WRITE | SECTION_MAP_WRITE,
    STANDARD_RIGHTS_EXECUTE | SECTION_MAP_EXECUTE,
    SECTION_ALL_ACCESS
};

NTSTATUS
MiPagingIo(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_ PVOID Buffer,
    _In_ BOOLEAN Write,
    _Out_ PULONG Transferred)
{
    UCHAR Storage[sizeof(MDL) + sizeof(PFN_NUMBER) * (MI_HEADER_BUFFER_SIZE / PAGE_SIZE + 1)];
    PMDL Mdl = (PMDL)Storage;
    ULONG Pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Buffer, Length);
    PPFN_NUMBER Frames;
    ULONG i;

    *Transferred = 0;
    ASSERT(Pages <= MI_HEADER_BUFFER_SIZE / PAGE_SIZE + 1);

    MmInitializeMdl(Mdl, Buffer, Length);
    Mdl->Process = NULL;
    Frames = MmGetMdlPfnArray(Mdl);

    for (i = 0; i < Pages; i++)
    {
        ULONG64 Physical = MiGetPhysicalAddress(&MiSystem.SystemSpace,
                                                (ULONG64)(ULONG_PTR)PAGE_ALIGN(Buffer) + (ULONG64)i * PAGE_SIZE);

        if (Physical == 0)
            return STATUS_INVALID_PARAMETER;

        Frames[i] = (PFN_NUMBER)(Physical >> PAGE_SHIFT);
    }

    Mdl->MdlFlags |= MDL_PAGES_LOCKED | MDL_MAPPED_TO_SYSTEM_VA;
    Mdl->MappedSystemVa = Buffer;

    if (!Write)
        Mdl->MdlFlags |= MDL_IO_PAGE_READ;

    return MiSubmitPagingMdl(FileObject, Mdl, Offset, Write, Transferred);
}

static
VOID
MiFreeControlArea(
    _Inout_ PMI_CONTROL_AREA Control)
{
    if (Control->FileObject != NULL)
        ObDereferenceObject(Control->FileObject);

    ExFreePoolWithTag(Control, 'aCmM');
}

static
VOID
MiControlRelease(
    _In_opt_ PVOID Context)
{
    PMI_CONTROL_AREA Control = Context;

    MI_RW_ACQUIRE_EXCLUSIVE(&MiControlLock);

    if (Control->FileObject != NULL && Control->FileObject->SectionObjectPointer != NULL)
    {
        PSECTION_OBJECT_POINTERS Pointers = Control->FileObject->SectionObjectPointer;

        if (Control->Image && Pointers->ImageSectionObject == Control)
            Pointers->ImageSectionObject = NULL;
        else if (!Control->Image && Pointers->DataSectionObject == Control)
            Pointers->DataSectionObject = NULL;
    }

    MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);

    MiFreeControlArea(Control);
}

static const ACCESS_MASK MiSectionAccessForProtection[8] =
{
    SECTION_MAP_READ,
    SECTION_MAP_READ,
    SECTION_MAP_EXECUTE,
    SECTION_MAP_EXECUTE | SECTION_MAP_READ,
    SECTION_MAP_WRITE,
    SECTION_MAP_READ,
    SECTION_MAP_EXECUTE | SECTION_MAP_WRITE,
    SECTION_MAP_EXECUTE | SECTION_MAP_READ
};

static
VOID
MiNormalizeImageHeaderPage(
    _Inout_ PUCHAR Page)
{
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Page;
    PIMAGE_SECTION_HEADER SectionHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG64 First;
    USHORT Count;
    USHORT i;

    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE || DosHeader->e_lfanew <= 0 ||
        (ULONG)DosHeader->e_lfanew > PAGE_SIZE - FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader))
    {
        return;
    }

    NtHeaders = (PIMAGE_NT_HEADERS)(Page + DosHeader->e_lfanew);
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
        return;

    First = (ULONG64)DosHeader->e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) +
            NtHeaders->FileHeader.SizeOfOptionalHeader;
    Count = NtHeaders->FileHeader.NumberOfSections;
    SectionHeader = (PIMAGE_SECTION_HEADER)(Page + First);

    for (i = 0; i < Count && First + ((ULONG64)i + 1) * sizeof(IMAGE_SECTION_HEADER) <= PAGE_SIZE; i++)
    {
        if (SectionHeader[i].SizeOfRawData == 0)
            SectionHeader[i].PointerToRawData = 0;
    }
}

static
NTSTATUS
MiControlImageRead(
    _In_opt_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _Out_ PVOID Buffer)
{
    NTSTATUS Status = MiControlRead(Context, Offset, Length, Buffer);

    if (NT_SUCCESS(Status) && Offset == 0)
        MiNormalizeImageHeaderPage(Buffer);

    return Status;
}

/* Called only by the modified page writer threads, which run with no top-level
 * IRP of their own. */
static
NTSTATUS
MiControlAcquireForModWrite(
    _In_opt_ PVOID Context,
    _In_ ULONG64 EndingOffset,
    _Out_ PVOID *Token)
{
    PMI_CONTROL_AREA Control = Context;
    LARGE_INTEGER End;
    NTSTATUS Status;

    *Token = NULL;
    End.QuadPart = (LONGLONG)EndingOffset;
    Status = FsRtlAcquireFileForModWriteEx(Control->FileObject, &End, (PERESOURCE *)Token);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The file system then knows the write comes from the modified writer. */
    ASSERT(IoGetTopLevelIrp() == NULL);
    IoSetTopLevelIrp((PIRP)FSRTL_MOD_WRITE_TOP_LEVEL_IRP);
    return STATUS_SUCCESS;
}

static
VOID
MiControlReleaseForModWrite(
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Token)
{
    PMI_CONTROL_AREA Control = Context;

    IoSetTopLevelIrp(NULL);
    if (Token != NULL)
        FsRtlReleaseFileForModWrite(Control->FileObject, (PERESOURCE)Token);
}

static
NTSTATUS
MiControlAcquireForFlush(
    _In_opt_ PVOID Context)
{
    PMI_CONTROL_AREA Control = Context;

    return FsRtlAcquireFileForCcFlushEx(Control->FileObject);
}

static
VOID
MiControlReleaseForFlush(
    _In_opt_ PVOID Context)
{
    PMI_CONTROL_AREA Control = Context;

    FsRtlReleaseFileForCcFlush(Control->FileObject);
}

static MI_FILE_OPS MiControlFileOps =
{
    .Read = MiControlRead,
    .Write = MiControlWrite,
    .Release = MiControlRelease,
    .WriteFrames = MiControlWriteFrames,
    .ReadAsync = MiControlReadAsync,
    .WholePageReads = TRUE,
    .ReadPages = MiControlReadPages,
    .AcquireForModWrite = MiControlAcquireForModWrite,
    .ReleaseForModWrite = MiControlReleaseForModWrite,
    .AcquireForFlush = MiControlAcquireForFlush,
    .ReleaseForFlush = MiControlReleaseForFlush
};
static MI_FILE_OPS MiControlImageOps =
{
    .Read = MiControlImageRead,
    .Write = MiControlWrite,
    .Release = MiControlRelease,
    .WriteFrames = MiControlWriteFrames,
    .ReadAsync = MiControlReadAsync,
    .ReadPages = MiControlReadPages,
    .AcquireForModWrite = MiControlAcquireForModWrite,
    .ReleaseForModWrite = MiControlReleaseForModWrite,
    .AcquireForFlush = MiControlAcquireForFlush,
    .ReleaseForFlush = MiControlReleaseForFlush
};
static MI_FILE_OPS MiControlAnonymousOps = { .Release = MiControlRelease };

VOID
MiDereferenceControlArea(
    _Inout_ PMI_CONTROL_AREA Control)
{
    MiSegmentDereference(Control->Segment);
}

static
BOOLEAN
MiLookupControlArea(
    _In_ PSECTION_OBJECT_POINTERS Pointers,
    _In_ BOOLEAN Image,
    _Out_ PMI_CONTROL_AREA *ControlOut)
{
    PMI_CONTROL_AREA Control = Image ? Pointers->ImageSectionObject : Pointers->DataSectionObject;

    *ControlOut = NULL;

    if (Control == NULL)
        return TRUE;

    if (!MiSegmentTryReference(Control->Segment))
        return FALSE;

    *ControlOut = Control;
    return TRUE;
}

static
VOID
MiWaitForControlTeardown(VOID)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

PMI_CONTROL_AREA
MiReferenceDataControlArea(
    _In_ PSECTION_OBJECT_POINTERS Pointers)
{
    PMI_CONTROL_AREA Control;

    MI_RW_ACQUIRE_SHARED(&MiControlLock);
    MiLookupControlArea(Pointers, FALSE, &Control);
    MI_RW_RELEASE_SHARED(&MiControlLock);

    return Control;
}

static
PMI_CONTROL_AREA
MiAllocateControlArea(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Image)
{
    PMI_CONTROL_AREA Control = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Control), 'aCmM');

    if (Control == NULL)
        return NULL;

    RtlZeroMemory(Control, sizeof(*Control));
    Control->Image = Image;

    if (FileObject != NULL)
    {
        ObReferenceObject(FileObject);
        Control->FileObject = FileObject;
    }

    return Control;
}

NTSTATUS
MiCreateDataControlArea(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG64 Size,
    _Out_ PMI_CONTROL_AREA *ControlOut)
{
    PSECTION_OBJECT_POINTERS Pointers = FileObject->SectionObjectPointer;
    PMI_CONTROL_AREA Control;
    NTSTATUS Status;

    *ControlOut = NULL;

    if (Pointers == NULL)
        return STATUS_INVALID_FILE_FOR_SECTION;

    Control = MiReferenceDataControlArea(Pointers);
    if (Control != NULL)
        goto Referenced;

    MI_RW_ACQUIRE_EXCLUSIVE(&MiControlLock);

    while (!MiLookupControlArea(Pointers, FALSE, &Control))
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        MiWaitForControlTeardown();
        MI_RW_ACQUIRE_EXCLUSIVE(&MiControlLock);
    }

    if (Control != NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        goto Referenced;
    }

    Control = MiAllocateControlArea(FileObject, FALSE);
    if (Control == NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = MiSegmentCreate(&MiSystem, MiSegmentDataFile, Size, MI_PROT_EXECUTE_READWRITE, &MiControlFileOps,
                             Control, NULL, 0, &Control->Segment);
    if (!NT_SUCCESS(Status))
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        MiFreeControlArea(Control);
        return Status;
    }

    Pointers->DataSectionObject = Control;
    MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);

    *ControlOut = Control;
    return STATUS_SUCCESS;

Referenced:
    Status = MiSegmentExtend(Control->Segment, Size);
    if (!NT_SUCCESS(Status))
    {
        MiDereferenceControlArea(Control);
        return Status;
    }
    *ControlOut = Control;
    return STATUS_SUCCESS;
}

static
ULONG
MiImageSectionProtection(
    _In_ ULONG Characteristics)
{
    BOOLEAN Execute = (BOOLEAN)((Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0);

    if (Characteristics & IMAGE_SCN_MEM_WRITE)
        return Execute ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE;

    if (Characteristics & IMAGE_SCN_MEM_READ)
        return Execute ? MI_PROT_EXECUTE_READ : MI_PROT_READONLY;

    return Execute ? MI_PROT_EXECUTE : MI_PROT_READONLY;
}

static
NTSTATUS
MiBuildImageControlArea(
    _Inout_ PMI_CONTROL_AREA Control,
    _In_ ULONG64 FileSize)
{
    PSECTION_IMAGE_INFORMATION Information = &Control->ImageInformation;
    PIMAGE_SECTION_HEADER SectionHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_DOS_HEADER DosHeader;
    PMI_SEGMENT_LAYOUT Layout = NULL;
    ULONG LayoutCount = 0;
    ULONG SectionAlignment, FileAlignment, SizeOfHeaders, SizeOfImage;
    ULONG HeaderBytes = (ULONG)min(FileSize, (ULONG64)MI_HEADER_BUFFER_SIZE);
    ULONG Transferred;
    NTSTATUS Status;
    PUCHAR Buffer;
    USHORT i;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, MI_HEADER_BUFFER_SIZE, 'hImM');
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Buffer, MI_HEADER_BUFFER_SIZE);
    Status = MiPagingIo(Control->FileObject, 0, ROUND_UP(HeaderBytes, 512), Buffer, FALSE, &Transferred);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = STATUS_INVALID_IMAGE_FORMAT;
    DosHeader = (PIMAGE_DOS_HEADER)Buffer;

    if (HeaderBytes < sizeof(IMAGE_DOS_HEADER) || DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        Status = STATUS_INVALID_IMAGE_NOT_MZ;
        goto Done;
    }

    if (DosHeader->e_lfanew <= 0 ||
        (ULONG)DosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > HeaderBytes)
    {
        Status = STATUS_INVALID_IMAGE_PROTECT;
        goto Done;
    }

    NtHeaders = (PIMAGE_NT_HEADERS)(Buffer + DosHeader->e_lfanew);
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
        goto Done;

    RtlZeroMemory(Information, sizeof(*Information));

    if (NtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        PIMAGE_OPTIONAL_HEADER64 Optional = &((PIMAGE_NT_HEADERS64)NtHeaders)->OptionalHeader;

        Control->Image64 = TRUE;
        Control->BasedAddress = (PVOID)(ULONG_PTR)Optional->ImageBase;
        SectionAlignment = Optional->SectionAlignment;
        FileAlignment = Optional->FileAlignment;
        SizeOfHeaders = Optional->SizeOfHeaders;
        SizeOfImage = Optional->SizeOfImage;
        Information->MaximumStackSize = (SIZE_T)Optional->SizeOfStackReserve;
        Information->CommittedStackSize = (SIZE_T)Optional->SizeOfStackCommit;
        Information->SubSystemType = Optional->Subsystem;
        Information->SubSystemMinorVersion = Optional->MinorSubsystemVersion;
        Information->SubSystemMajorVersion = Optional->MajorSubsystemVersion;
        Information->DllCharacteristics = Optional->DllCharacteristics;
        Information->LoaderFlags = Optional->LoaderFlags;
        Information->CheckSum = Optional->CheckSum;
        Information->TransferAddress = Optional->AddressOfEntryPoint
                                           ? (PVOID)((ULONG_PTR)Optional->ImageBase + Optional->AddressOfEntryPoint)
                                           : NULL;
        Information->ImageContainsCode = (BOOLEAN)(Optional->SizeOfCode != 0 || Optional->AddressOfEntryPoint != 0);
    }
    else if (NtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        PIMAGE_OPTIONAL_HEADER32 Optional = &((PIMAGE_NT_HEADERS32)NtHeaders)->OptionalHeader;

        Control->BasedAddress = (PVOID)(ULONG_PTR)Optional->ImageBase;
        SectionAlignment = Optional->SectionAlignment;
        FileAlignment = Optional->FileAlignment;
        SizeOfHeaders = Optional->SizeOfHeaders;
        SizeOfImage = Optional->SizeOfImage;
        Information->MaximumStackSize = Optional->SizeOfStackReserve;
        Information->CommittedStackSize = Optional->SizeOfStackCommit;
        Information->SubSystemType = Optional->Subsystem;
        Information->SubSystemMinorVersion = Optional->MinorSubsystemVersion;
        Information->SubSystemMajorVersion = Optional->MajorSubsystemVersion;
        Information->DllCharacteristics = Optional->DllCharacteristics;
        Information->LoaderFlags = Optional->LoaderFlags;
        Information->CheckSum = Optional->CheckSum;
        Information->TransferAddress = Optional->AddressOfEntryPoint
                                           ? (PVOID)((ULONG_PTR)Optional->ImageBase + Optional->AddressOfEntryPoint)
                                           : NULL;
        Information->ImageContainsCode = (BOOLEAN)(Optional->SizeOfCode != 0 || Optional->AddressOfEntryPoint != 0);
    }
    else
    {
        goto Done;
    }

    Information->ImageCharacteristics = NtHeaders->FileHeader.Characteristics;
    Information->Machine = NtHeaders->FileHeader.Machine;
    Information->ImageFileSize = (ULONG)FileSize;

    if (SizeOfImage == 0 || SectionAlignment == 0 || FileAlignment == 0 ||
        (SectionAlignment & (SectionAlignment - 1)) || (FileAlignment & (FileAlignment - 1)) ||
        SectionAlignment < FileAlignment)
    {
        goto Done;
    }

    Control->ImageSize = ROUND_TO_PAGES(SizeOfImage);

    SectionHeader = IMAGE_FIRST_SECTION(NtHeaders);
    if ((PUCHAR)(SectionHeader + NtHeaders->FileHeader.NumberOfSections) > Buffer + HeaderBytes)
    {
        Status = STATUS_INVALID_IMAGE_PROTECT;
        goto Done;
    }

    Layout = ExAllocatePoolWithTag(NonPagedPool,
                                   (NtHeaders->FileHeader.NumberOfSections + 1) * sizeof(MI_SEGMENT_LAYOUT), 'lImM');
    if (Layout == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    if (SectionAlignment < PAGE_SIZE || (FileAlignment & 511))
    {
        Information->ImageMappedFlat = 1;
        Layout[0].FirstPage = 0;
        Layout[0].PageCount = Control->ImageSize >> PAGE_SHIFT;
        Layout[0].FileOffset = 0;
        Layout[0].FileBytes = min(FileSize, Control->ImageSize);
        Layout[0].Protection = MI_PROT_EXECUTE_READWRITE;
        LayoutCount = 1;
    }
    else
    {
        Layout[0].FirstPage = 0;
        Layout[0].PageCount = BYTES_TO_PAGES(SizeOfHeaders);
        Layout[0].FileOffset = 0;
        Layout[0].FileBytes = min((ULONG64)SizeOfHeaders, FileSize);
        Layout[0].Protection = MI_PROT_READONLY;
        LayoutCount = 1;

        for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++)
        {
            ULONG VirtualSize = SectionHeader[i].Misc.VirtualSize ? SectionHeader[i].Misc.VirtualSize
                                                                   : SectionHeader[i].SizeOfRawData;
            ULONG64 RawOffset = SectionHeader[i].PointerToRawData & ~511ULL;
            ULONG64 RawBytes = SectionHeader[i].SizeOfRawData;
            PMI_SEGMENT_LAYOUT Entry = &Layout[LayoutCount];

            if ((ULONG64)SectionHeader[i].PointerToRawData + SectionHeader[i].SizeOfRawData > MAXULONG)
            {
                Status = STATUS_INVALID_IMAGE_FORMAT;
                goto Done;
            }

            if (VirtualSize == 0)
                continue;

            if ((SectionHeader[i].VirtualAddress & (PAGE_SIZE - 1)) ||
                (ULONG64)SectionHeader[i].VirtualAddress + VirtualSize > Control->ImageSize)
            {
                Status = STATUS_INVALID_IMAGE_FORMAT;
                goto Done;
            }

            if (SectionHeader[i].PointerToRawData == 0 ||
                (SectionHeader[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA))
            {
                RawBytes = (SectionHeader[i].PointerToRawData == 0) ? 0 : RawBytes;
            }

            if (RawBytes > ROUND_TO_PAGES(VirtualSize))
                RawBytes = ROUND_TO_PAGES(VirtualSize);

            if (RawOffset >= FileSize)
                RawBytes = 0;
            else if (RawOffset + RawBytes > FileSize)
                RawBytes = FileSize - RawOffset;

            Entry->FirstPage = SectionHeader[i].VirtualAddress >> PAGE_SHIFT;
            Entry->PageCount = BYTES_TO_PAGES(VirtualSize);
            Entry->FileOffset = RawOffset;
            Entry->FileBytes = RawBytes;
            Entry->Protection = MiImageSectionProtection(SectionHeader[i].Characteristics);
            LayoutCount++;
        }
    }

    Status = MiSegmentCreate(&MiSystem, MiSegmentImage, Control->ImageSize, MI_PROT_EXECUTE_READ, &MiControlImageOps,
                             Control, Layout, LayoutCount, &Control->Segment);

Done:
    if (Layout != NULL)
        ExFreePoolWithTag(Layout, 'lImM');

    ExFreePoolWithTag(Buffer, 'hImM');
    return Status;
}

static
NTSTATUS
MiCreateImageControlArea(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PMI_CONTROL_AREA *ControlOut)
{
    PSECTION_OBJECT_POINTERS Pointers = FileObject->SectionObjectPointer;
    PMI_CONTROL_AREA Control;
    LARGE_INTEGER FileSize;
    NTSTATUS Status;

    *ControlOut = NULL;

    if (Pointers == NULL)
        return STATUS_INVALID_FILE_FOR_SECTION;

    Status = FsRtlGetFileSize(FileObject, &FileSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (FileSize.QuadPart == 0)
        return STATUS_INVALID_FILE_FOR_SECTION;

    if (Pointers->SharedCacheMap != NULL || Pointers->DataSectionObject != NULL)
    {
        IO_STATUS_BLOCK IoStatus;

        CcFlushCache(Pointers, NULL, 0, &IoStatus);
        if (!NT_SUCCESS(IoStatus.Status))
            return IoStatus.Status;
    }

    MI_RW_ACQUIRE_EXCLUSIVE(&MiControlLock);

    while (!MiLookupControlArea(Pointers, TRUE, &Control))
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        MiWaitForControlTeardown();
        MI_RW_ACQUIRE_EXCLUSIVE(&MiControlLock);
    }

    if (Control != NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        *ControlOut = Control;
        return STATUS_SUCCESS;
    }

    Control = MiAllocateControlArea(FileObject, TRUE);
    if (Control == NULL)
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = MiBuildImageControlArea(Control, (ULONG64)FileSize.QuadPart);
    if (!NT_SUCCESS(Status))
    {
        MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);
        MiFreeControlArea(Control);
        return Status;
    }

    Pointers->ImageSectionObject = Control;
    MI_RW_RELEASE_EXCLUSIVE(&MiControlLock);

    *ControlOut = Control;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
MiDeleteSection(
    _In_ PVOID ObjectBody)
{
    PMI_SECTION_OBJECT Section = ObjectBody;

    if (Section->Control == NULL)
        return;

    if (Section->Control->Physical)
        ExFreePoolWithTag(Section->Control, 'aCmM');
    else
        MiDereferenceControlArea(Section->Control);
}

static
NTSTATUS
MiCreatePhysicalMemorySection(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\PhysicalMemory");
    OBJECT_ATTRIBUTES ObjectAttributes;
    PMI_SECTION_OBJECT Section;
    PMI_CONTROL_AREA Control;
    NTSTATUS Status;
    HANDLE Handle;

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_PERMANENT | OBJ_KERNEL_EXCLUSIVE, NULL, NULL);

    Status = ObCreateObject(KernelMode, MmSectionObjectType, &ObjectAttributes, KernelMode, NULL,
                            sizeof(MI_SECTION_OBJECT), 0, 0, (PVOID *)&Section);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Section, sizeof(*Section));

    Control = MiAllocateControlArea(NULL, FALSE);
    if (Control == NULL)
    {
        ObDereferenceObject(Section);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Control->Physical = TRUE;
    Section->Control = Control;
    Section->SizeOfSection.QuadPart = (LONGLONG)((ULONG64)(MmHighestPhysicalPage + 1) << PAGE_SHIFT);
    Section->InitialProtection = PAGE_EXECUTE_READWRITE;
    Section->Protection = MI_PROT_EXECUTE_READWRITE;
    Section->AllocationAttributes = 0;

    Status = ObInsertObject(Section, NULL, SECTION_ALL_ACCESS, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    ObCloseHandle(Handle, KernelMode);
    return STATUS_SUCCESS;
}

static
NTSTATUS
MiAllocateBasedAddress(
    _In_ ULONG64 Size,
    _Out_ PVOID *BasedAddress)
{
    ULONG64 Top = (((ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1) / 2) & ~(ULONG64)(MI_ALLOCATION_GRANULARITY - 1);
    ULONG64 Length;

    if (Size == 0 || Size > Top - MI_ALLOCATION_GRANULARITY)
        return STATUS_NO_MEMORY;

    Length = (Size + MI_ALLOCATION_GRANULARITY - 1) & ~(ULONG64)(MI_ALLOCATION_GRANULARITY - 1);

    for (;;)
    {
        LONG64 Old = InterlockedCompareExchange64(&MiBasedSectionCursor, 0, 0);
        ULONG64 Current = (Old != 0) ? (ULONG64)Old : Top;

        if (Length > Current - MI_ALLOCATION_GRANULARITY)
            return STATUS_NO_MEMORY;

        if (InterlockedCompareExchange64(&MiBasedSectionCursor, (LONG64)(Current - Length), Old) == Old)
        {
            *BasedAddress = (PVOID)(ULONG_PTR)(Current - Length);
            return STATUS_SUCCESS;
        }
    }
}

NTSTATUS
MiSectionInitialize(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name;
    NTSTATUS Status;

    MI_RW_INIT(&MiControlLock);

    RtlZeroMemory(&Initializer, sizeof(Initializer));
    RtlInitUnicodeString(&Name, L"Section");
    Initializer.Length = sizeof(Initializer);
    Initializer.ObjectTypeCode = MI_NT_SECTION_TYPE_CODE;
    Initializer.DefaultPagedPoolCharge = MI_NT_SECTION_PAGED_CHARGE;
    Initializer.PoolType = PagedPool;
    Initializer.UseDefaultObject = TRUE;
    Initializer.GenericMapping = MiSectionMapping;
    Initializer.DeleteProcedure = MiDeleteSection;
    Initializer.ValidAccessMask = SECTION_ALL_ACCESS;
    Initializer.InvalidAttributes = OBJ_OPENLINK;

    Status = ObCreateObjectType(&Name, &Initializer, NULL, &MmSectionObjectType);
    if (!NT_SUCCESS(Status))
        return Status;

    MiCreatePhysicalMemorySection();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmCreateSection(
    _Out_ PVOID *SectionObject,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Inout_ PLARGE_INTEGER MaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_opt_ HANDLE FileHandle,
    _In_opt_ PFILE_OBJECT FileObject)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PFILE_OBJECT File = FileObject;
    PMI_CONTROL_AREA Control = NULL;
    PMI_SECTION_OBJECT Section;
    PVOID BasedAddress = NULL;
    BOOLEAN FileReferenced = FALSE;
    ULONG64 Size = 0;
    ULONG Protection;
    NTSTATUS Status;

    if (!(AllocationAttributes & (SEC_COMMIT | SEC_RESERVE | SEC_IMAGE)))
        return STATUS_INVALID_PARAMETER_6;

    if ((AllocationAttributes & (SEC_COMMIT | SEC_RESERVE)) == (SEC_COMMIT | SEC_RESERVE))
        return STATUS_INVALID_PARAMETER_6;

    if ((AllocationAttributes & SEC_IMAGE) && (AllocationAttributes & (SEC_COMMIT | SEC_RESERVE | SEC_NOCACHE)))
        return STATUS_INVALID_PARAMETER_6;

    if (AllocationAttributes & SEC_LARGE_PAGES)
    {
        ULONG64 LargePageMinimum = MiSystem.Arch->LargePageSize;

        if (!MiSystem.Arch->SupportsLargePages || LargePageMinimum == 0)
            return STATUS_NOT_SUPPORTED;

        if (!(AllocationAttributes & SEC_COMMIT) || FileHandle != NULL || FileObject != NULL || MaximumSize == NULL)
            return STATUS_INVALID_PARAMETER_6;

        if (MaximumSize->QuadPart <= 0 || (MaximumSize->QuadPart % LargePageMinimum) != 0)
            return STATUS_INVALID_PARAMETER_4;

        if (!SeSinglePrivilegeCheck(SeLockMemoryPrivilege, PreviousMode))
            return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!(AllocationAttributes & SEC_IMAGE))
        Size = (ULONG64)MaximumSize->QuadPart;

    if (!MiProtectionFromWin32(SectionPageProtection, &Protection) || !MI_PROT_IS_ACCESSIBLE(Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    if (File != NULL && (AllocationAttributes & SEC_IMAGE))
        return STATUS_INVALID_PARAMETER_6;

    if (File == NULL && FileHandle == NULL && (AllocationAttributes & SEC_IMAGE))
        return STATUS_INVALID_FILE_FOR_SECTION;

    if (File == NULL && FileHandle != NULL)
    {
        static const ACCESS_MASK FileAccessForProtection[8] =
        {
            FILE_READ_DATA,
            FILE_READ_DATA,
            FILE_EXECUTE,
            FILE_EXECUTE | FILE_READ_DATA,
            FILE_WRITE_DATA | FILE_READ_DATA,
            FILE_READ_DATA,
            FILE_EXECUTE | FILE_WRITE_DATA | FILE_READ_DATA,
            FILE_EXECUTE | FILE_READ_DATA
        };
        ACCESS_MASK FileAccess = FileAccessForProtection[Protection & MI_PROT_ACCESS_MASK];

        Status = ObReferenceObjectByHandle(FileHandle, FileAccess, IoFileObjectType, PreviousMode, (PVOID *)&File,
                                           NULL);
        if (!NT_SUCCESS(Status))
            return Status;

        FileReferenced = TRUE;
    }

    if (File != NULL && File->SectionObjectPointer == NULL)
    {
        if (FileReferenced)
            ObDereferenceObject(File);

        return STATUS_INVALID_FILE_FOR_SECTION;
    }

    if (File == NULL)
    {
        ULONG64 SizeLimit = ((ULONG64)MmSizeOfPagedPoolInBytes / sizeof(MI_PTE)) << PAGE_SHIFT;

        if (Size == 0)
            return STATUS_INVALID_PARAMETER_4;

        if (Size > (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1)
            return STATUS_SECTION_TOO_BIG;

        if (Size > SizeLimit)
            return STATUS_INSUFFICIENT_RESOURCES;

        Size = ROUND_TO_PAGES(Size);

        Control = MiAllocateControlArea(NULL, FALSE);
        if (Control == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = (AllocationAttributes & SEC_LARGE_PAGES)
            ? MiSegmentCreateLarge(&MiSystem, Size, Protection, &MiControlAnonymousOps, Control, &Control->Segment)
            : (AllocationAttributes & SEC_RESERVE)
            ? MiSegmentCreateReserved(&MiSystem, Size, MI_PROT_EXECUTE_READWRITE, &MiControlAnonymousOps, Control,
                                      &Control->Segment)
            : MiSegmentCreate(&MiSystem, MiSegmentPageFileBacked, Size, MI_PROT_EXECUTE_READWRITE,
                               &MiControlAnonymousOps, Control, NULL, 0, &Control->Segment);
        if (!NT_SUCCESS(Status))
        {
            MiFreeControlArea(Control);
            return Status;
        }
    }
    else if (AllocationAttributes & SEC_IMAGE)
    {
        Status = MiValidateImageSigningPolicy(File);
        if (NT_SUCCESS(Status))
            Status = MiCreateImageControlArea(File, &Control);
        if (NT_SUCCESS(Status))
            Size = (MaximumSize != NULL && MaximumSize->QuadPart != 0) ? (ULONG64)MaximumSize->QuadPart
                                                                      : Control->ImageSize;
    }
    else
    {
        LARGE_INTEGER FileSize;

        Status = FsRtlGetFileSize(File, &FileSize);

        if (NT_SUCCESS(Status) && Size == 0)
        {
            Size = (ULONG64)FileSize.QuadPart;
            if (Size == 0)
                Status = STATUS_MAPPED_FILE_SIZE_ZERO;
        }
        else if (NT_SUCCESS(Status) && Size > (ULONG64)FileSize.QuadPart)
        {
            if (!MI_PROT_IS_WRITABLE(Protection))
            {
                Status = STATUS_SECTION_TOO_BIG;
            }
            else
            {
                FILE_END_OF_FILE_INFORMATION EndOfFile;

                EndOfFile.EndOfFile.QuadPart = (LONGLONG)Size;
                Status = IoSetInformation(File, FileEndOfFileInformation, sizeof(EndOfFile), &EndOfFile);
            }
        }

        if (NT_SUCCESS(Status))
            Status = MiCreateDataControlArea(File, Size, &Control);
    }

    if (FileReferenced)
        ObDereferenceObject(File);

    if (!NT_SUCCESS(Status))
        return Status;

    if ((AllocationAttributes & SEC_BASED) && !Control->Image)
    {
        Status = MiAllocateBasedAddress(Size, &BasedAddress);
        if (!NT_SUCCESS(Status))
        {
            MiDereferenceControlArea(Control);
            return Status;
        }
    }

    Status = ObCreateObject(PreviousMode, MmSectionObjectType, ObjectAttributes, PreviousMode, NULL,
                            sizeof(MI_SECTION_OBJECT), 0, 0, (PVOID *)&Section);
    if (!NT_SUCCESS(Status))
    {
        MiDereferenceControlArea(Control);
        return Status;
    }

    RtlZeroMemory(Section, sizeof(*Section));
    Section->Control = Control;
    Section->SizeOfSection.QuadPart = (LONGLONG)Size;
    Section->InitialProtection = SectionPageProtection;
    Section->Protection = Protection;
    Section->AllocationAttributes = AllocationAttributes;
    Section->BasedAddress = BasedAddress;

    *SectionObject = Section;
    UNREFERENCED_PARAMETER(DesiredAccess);
    return STATUS_SUCCESS;
}

BOOLEAN
MiFrameIsRam(
    _In_ ULONG64 Frame)
{
    ULONG i;

    if (MmPhysicalMemoryBlock == NULL)
        return FALSE;

    for (i = 0; i < MmPhysicalMemoryBlock->NumberOfRuns; i++)
    {
        if (Frame >= MmPhysicalMemoryBlock->Run[i].BasePage &&
            Frame < MmPhysicalMemoryBlock->Run[i].BasePage + MmPhysicalMemoryBlock->Run[i].PageCount)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
NTSTATUS
MiMapPhysicalView(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PVOID *BaseAddress,
    _Inout_opt_ PLARGE_INTEGER SectionOffset,
    _Inout_ PSIZE_T ViewSize,
    _In_ ULONG Protection,
    _In_ ULONG Win32Protect)
{
    ULONG64 Physical = (SectionOffset != NULL) ? (ULONG64)SectionOffset->QuadPart : 0;
    ULONG64 Delta = Physical & (PAGE_SIZE - 1);
    ULONG64 Size = ROUND_TO_PAGES((ULONG64)*ViewSize + Delta);
    ULONG64 Base = (ULONG64)(ULONG_PTR)PAGE_ALIGN(*BaseAddress);
    ULONG64 FirstFrame = Physical >> PAGE_SHIFT;
    ULONG LeafFlags = 0;
    PMI_FRAME_NUMBER Frames;
    NTSTATUS Status;
    ULONG Count;
    ULONG i;

    if (Space->IsSystem)
        return STATUS_NOT_SUPPORTED;

    if (*ViewSize == 0 || Size > MAXULONG)
        return STATUS_INVALID_VIEW_SIZE;

    Count = (ULONG)(Size >> PAGE_SHIFT);

    if (!MiFrameIsRam(FirstFrame))
        LeafFlags = MI_LEAF_DEVICE;
    else if (Win32Protect & PAGE_NOCACHE)
        LeafFlags = MI_LEAF_NOCACHE;
    else if (Win32Protect & PAGE_WRITECOMBINE)
        LeafFlags = MI_LEAF_WRITECOMBINE;

    Frames = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Count * sizeof(*Frames), 'hPmM');
    if (Frames == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < Count; i++)
        Frames[i] = (MI_FRAME_NUMBER)(FirstFrame + i);

    Status = MiMapFramesUser(Space, Frames, Count, Protection & ~MI_PROT_NOCACHE, LeafFlags, FALSE, &Base);
    ExFreePoolWithTag(Frames, 'hPmM');

    if (!NT_SUCCESS(Status))
        return Status;

    *BaseAddress = (PVOID)(ULONG_PTR)Base;
    *ViewSize = (SIZE_T)Size;

    if (SectionOffset != NULL)
        SectionOffset->QuadPart = (LONGLONG)(Physical - Delta);

    return STATUS_SUCCESS;
}

static
NTSTATUS
MiMapSectionView(
    _In_ PMI_SECTION_OBJECT Section,
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PVOID *BaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _Inout_opt_ PLARGE_INTEGER SectionOffset,
    _Inout_ PSIZE_T ViewSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Win32Protect,
    _In_ BOOLEAN Inherit)
{
    PMI_CONTROL_AREA Control = Section->Control;
    ULONG64 Offset = (SectionOffset != NULL) ? (ULONG64)SectionOffset->QuadPart : 0;
    ULONG64 Base = (ULONG64)(ULONG_PTR)*BaseAddress;
    ULONG64 Size = *ViewSize;
    ULONG64 Highest = ~0ULL;
    ULONG Attempts = 0;
    ULONG Protection;
    NTSTATUS Status;

    if (!MiProtectionFromWin32(Win32Protect, &Protection))
    {
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    if (Control->Physical)
        return MiMapPhysicalView(Space, BaseAddress, SectionOffset, ViewSize, Protection, Win32Protect);

    if (AllocationType & MEM_LARGE_PAGES)
    {
        if (Control->Segment->LargeFrames == NULL)
            return STATUS_INVALID_PARAMETER_9;
        if (((Base | Offset | Size) & (MiSystem.Arch->LargePageSize - 1)) != 0)
            return STATUS_MAPPED_ALIGNMENT;
    }

    if (Protection != MI_PROT_NOACCESS)
        Protection &= ~MI_PROT_NOCACHE;

    if (!Space->IsSystem)
    {
        Highest = (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;

        if (ZeroBits != 0 && ZeroBits < 32)
            Highest = min(Highest, ~0ULL >> (ZeroBits + 32));
        else if (ZeroBits >= 32)
            Highest = min(Highest, (ULONG64)ZeroBits);
    }

    if (Control->Image)
    {
        if (Offset != 0)
            return STATUS_INVALID_PARAMETER_6;

        Size = 0;

        if (Base == 0 && !Space->IsSystem)
            Base = (ULONG64)(ULONG_PTR)Control->BasedAddress & ~(MI_ALLOCATION_GRANULARITY - 1);
    }
    else
    {
        ULONG64 Delta = Offset & (MI_ALLOCATION_GRANULARITY - 1);
        ULONG64 SectionSize = (ULONG64)Section->SizeOfSection.QuadPart;

        if (Offset >= SectionSize || Size > ~0ULL - Offset ||
            (!(AllocationType & MEM_RESERVE) && Size > SectionSize - Offset))
            return STATUS_INVALID_VIEW_SIZE;

        Offset -= Delta;

        if (Base == 0 && !Space->IsSystem && Section->BasedAddress != NULL)
            Base = (ULONG64)(ULONG_PTR)Section->BasedAddress + Offset;

        if (Size == 0)
            Size = SectionSize - Offset;
        else
            Size += Delta;

        if (Size > ~0ULL - (PAGE_SIZE - 1))
            return STATUS_INVALID_VIEW_SIZE;
    }

    Base &= ~(MI_ALLOCATION_GRANULARITY - 1);

    for (;;)
    {
        ULONG64 TryBase = Base;
        ULONG64 TrySize = Size;

        Status = MiMapViewEx(Space, Control->Segment, &TryBase, Offset, &TrySize, Protection,
                             AllocationType & (MEM_TOP_DOWN | MEM_RESERVE | MEM_LARGE_PAGES), Highest,
                             Control->Image ? 0 : Section->Protection, Inherit);

        if (NT_SUCCESS(Status))
        {
            Base = TryBase;
            Size = TrySize;
            break;
        }

        if (Status == STATUS_CONFLICTING_ADDRESSES && Control->Image && *BaseAddress == NULL && Base != 0)
        {
            Base = 0;
            continue;
        }

        if (Status != STATUS_NO_MEMORY || !NT_SUCCESS(MiWaitForMemory(Status, &Attempts)))
            break;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    *BaseAddress = (PVOID)(ULONG_PTR)Base;
    *ViewSize = (SIZE_T)Size;

    if (SectionOffset != NULL)
        SectionOffset->QuadPart = (LONGLONG)Offset;

    if (Control->Image && !Space->IsSystem &&
        Base != ((ULONG64)(ULONG_PTR)Control->BasedAddress & ~(MI_ALLOCATION_GRANULARITY - 1)))
    {
        return STATUS_IMAGE_NOT_AT_BASE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmMapViewOfSection(
    _In_ PVOID SectionObject,
    _In_ PEPROCESS Process,
    _Inout_ PVOID *BaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _In_ SIZE_T CommitSize,
    _Inout_opt_ PLARGE_INTEGER SectionOffset,
    _Inout_ PSIZE_T ViewSize,
    _In_ SECTION_INHERIT InheritDisposition,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect)
{
    PMI_SECTION_OBJECT Section = SectionObject;
    BOOLEAN Attached = FALSE;
    KAPC_STATE ApcState;
    NTSTATUS Status;

    if (InheritDisposition != ViewShare && InheritDisposition != ViewUnmap)
        return STATUS_INVALID_PARAMETER;

    if (MI_PROCESS_OF(Process) == NULL)
        return STATUS_PROCESS_IS_TERMINATING;

    if (CommitSize != 0 && !Section->Control->Image && Section->Control->Segment != NULL &&
        Section->Control->Segment->Reserved)
    {
        Status = MiSegmentCommitPages(Section->Control->Segment,
                                      (SectionOffset != NULL) ? ((ULONG64)SectionOffset->QuadPart >> PAGE_SHIFT) : 0,
                                      ((ULONG64)CommitSize + PAGE_SIZE - 1) >> PAGE_SHIFT);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    Status = MiMapSectionView(Section, MiSpaceOfProcess(Process), BaseAddress, ZeroBits, SectionOffset, ViewSize,
                              AllocationType, Protect, (BOOLEAN)(InheritDisposition == ViewShare));

    if (NT_SUCCESS(Status))
    {
        Process->VirtualSize += *ViewSize;
        if (Process->VirtualSize > Process->PeakVirtualSize)
            Process->PeakVirtualSize = Process->VirtualSize;
    }

    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    return Status;
}

NTSTATUS
NTAPI
MmUnmapViewOfSection(
    _In_ PEPROCESS Process,
    _In_ PVOID BaseAddress)
{
    BOOLEAN Attached = FALSE;
    KAPC_STATE ApcState;
    NTSTATUS Status;

    if (MI_PROCESS_OF(Process) == NULL)
        return STATUS_PROCESS_IS_TERMINATING;

    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    Status = MiUnmapView(MiSpaceOfProcess(Process), (ULONG64)(ULONG_PTR)BaseAddress);
    if (Status == STATUS_NOT_MAPPED_VIEW)
        Status = MiUnmapFramesUser(MiSpaceOfProcess(Process), (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress), FALSE);

    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    return Status;
}

NTSTATUS
NTAPI
MmMapViewInSystemSpaceEx(
    _In_ PVOID SectionObject,
    _Outptr_result_bytebuffer_(*ViewSize) PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize,
    _Inout_ PLARGE_INTEGER SectionOffset,
    _In_ ULONG_PTR Flags)
{
    PMI_SECTION_OBJECT Section = SectionObject;

    UNREFERENCED_PARAMETER(Flags);

    *MappedBase = NULL;
    return MiMapSectionView(Section, &MiSystem.SystemSpace, MappedBase, 0, SectionOffset, ViewSize, 0,
                            MI_PROT_IS_WRITABLE(Section->Protection) ? PAGE_READWRITE : PAGE_READONLY, FALSE);
}

NTSTATUS
NTAPI
MmMapViewInSystemSpace(
    _In_ PVOID SectionObject,
    _Outptr_result_bytebuffer_(*ViewSize) PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize)
{
    LARGE_INTEGER Offset;

    Offset.QuadPart = 0;
    return MmMapViewInSystemSpaceEx(SectionObject, MappedBase, ViewSize, &Offset, 0);
}

NTSTATUS
NTAPI
MmUnmapViewInSystemSpace(
    _In_ PVOID MappedBase)
{
    return MiUnmapView(&MiSystem.SystemSpace, (ULONG64)(ULONG_PTR)MappedBase);
}

NTSTATUS
NTAPI
MmpMapViewInSessionSpaceEx(
    _In_ PVOID Section,
    _Out_ PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize,
    _Inout_ PLARGE_INTEGER SectionOffset,
    _In_ ULONG_PTR Flags)
{
    if (Flags != 0)
        return STATUS_NOT_SUPPORTED;

    return MmMapViewInSystemSpaceEx(Section, MappedBase, ViewSize, SectionOffset, 0);
}

NTSTATUS
NTAPI
MmMapViewInSessionSpace(
    _In_ PVOID Section,
    _Outptr_result_bytebuffer_(*ViewSize) PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize)
{
    return MmMapViewInSystemSpace(Section, MappedBase, ViewSize);
}

NTSTATUS
NTAPI
MmUnmapViewInSessionSpace(
    _In_ PVOID MappedBase)
{
    return MmUnmapViewInSystemSpace(MappedBase);
}

NTSTATUS
NTAPI
MmCommitSessionMappedView(
    _In_ PVOID MappedBase,
    _In_ SIZE_T ViewSize)
{
    UNREFERENCED_PARAMETER(MappedBase);
    UNREFERENCED_PARAMETER(ViewSize);
    return STATUS_SUCCESS;
}

PFILE_OBJECT
NTAPI
MmGetFileObjectForSection(
    _In_ PVOID SectionObject)
{
    PMI_SECTION_OBJECT Section = SectionObject;

    return Section->Control->FileObject;
}

PVOID
NTAPI
MmGetImageBaseForSection(
    _In_ PVOID SectionObject)
{
    PMI_SECTION_OBJECT Section = SectionObject;

    return Section->Control->BasedAddress;
}

NTSTATUS
NTAPI
MmGetFileNameForFileObject(
    _In_ PFILE_OBJECT FileObject,
    _Out_ POBJECT_NAME_INFORMATION *ModuleName)
{
    POBJECT_NAME_INFORMATION Information;
    ULONG Length = 1024;
    NTSTATUS Status;

    *ModuleName = NULL;

    Information = ExAllocatePoolWithTag(PagedPool, Length, 'nFmM');
    if (Information == NULL)
        return STATUS_NO_MEMORY;

    Status = ObQueryNameString(FileObject, Information, Length, &Length);
    if (Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        ExFreePoolWithTag(Information, 'nFmM');
        Information = ExAllocatePoolWithTag(PagedPool, Length, 'nFmM');
        if (Information == NULL)
            return STATUS_NO_MEMORY;

        Status = ObQueryNameString(FileObject, Information, Length, &Length);
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Information, 'nFmM');
        return Status;
    }

    *ModuleName = Information;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmGetFileNameForSection(
    _In_ PVOID SectionObject,
    _Out_ POBJECT_NAME_INFORMATION *ModuleName)
{
    PMI_SECTION_OBJECT Section = SectionObject;

    if (Section->Control->FileObject == NULL)
        return STATUS_SECTION_NOT_IMAGE;

    return MmGetFileNameForFileObject(Section->Control->FileObject, ModuleName);
}

static
PMI_CONTROL_AREA
MiReferenceControlForAddress(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ PVOID Address)
{
    PMI_CONTROL_AREA Control = NULL;
    PMI_VAD Vad;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Vad = MiVadLocate(Space, (ULONG64)(ULONG_PTR)Address);
    if (Vad != NULL && (Vad->Type == MiVadMapped || Vad->Type == MiVadImage) &&
        (Vad->Segment->FileOps.Read == MiControlRead || Vad->Segment->FileOps.Read == MiControlImageRead))
    {
        Control = Vad->Segment->FileContext;
        MiSegmentReference(Vad->Segment);
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Control;
}

NTSTATUS
NTAPI
MmGetFileNameForAddress(
    _In_ PVOID Address,
    _Out_ PUNICODE_STRING ModuleName)
{
    POBJECT_NAME_INFORMATION Information;
    PMI_CONTROL_AREA Control;
    NTSTATUS Status;

    Control = MiReferenceControlForAddress(MiSpaceForAddress(Address), Address);
    if (Control == NULL)
        return STATUS_INVALID_ADDRESS;

    Status = MmGetFileNameForFileObject(Control->FileObject, &Information);
    MiDereferenceControlArea(Control);

    if (!NT_SUCCESS(Status))
        return Status;

    ModuleName->Length = Information->Name.Length;
    ModuleName->MaximumLength = Information->Name.MaximumLength;
    ModuleName->Buffer = ExAllocatePoolWithTag(PagedPool, ModuleName->MaximumLength, 'nFmM');

    if (ModuleName->Buffer == NULL)
        Status = STATUS_NO_MEMORY;
    else
        RtlCopyMemory(ModuleName->Buffer, Information->Name.Buffer, Information->Name.Length);

    ExFreePoolWithTag(Information, 'nFmM');
    return Status;
}

NTSTATUS
MiQuerySectionName(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _Out_ PVOID MemoryInformation,
    _In_ SIZE_T MemoryInformationLength,
    _Out_opt_ PSIZE_T ReturnLength)
{
    PMEMORY_SECTION_NAME SectionName = MemoryInformation;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING ModuleName;
    KAPC_STATE ApcState;
    PEPROCESS Process;
    NTSTATUS Status;

    Status = ObReferenceObjectByHandle(ProcessHandle, PROCESS_QUERY_INFORMATION, PsProcessType, PreviousMode,
                                       (PVOID *)&Process, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    KeStackAttachProcess(&Process->Pcb, &ApcState);
    Status = MmGetFileNameForAddress(BaseAddress, &ModuleName);
    KeUnstackDetachProcess(&ApcState);
    ObDereferenceObject(Process);

    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(MemoryInformation, MemoryInformationLength, sizeof(ULONG_PTR));
            if (ReturnLength != NULL)
                ProbeForWriteSize_t(ReturnLength);
        }

        if (MemoryInformationLength < sizeof(MEMORY_SECTION_NAME) + ModuleName.Length + sizeof(WCHAR))
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        else
        {
            SectionName->SectionFileName.Buffer = (PWSTR)(SectionName + 1);
            SectionName->SectionFileName.Length = ModuleName.Length;
            SectionName->SectionFileName.MaximumLength = ModuleName.Length + sizeof(WCHAR);
            RtlCopyMemory(SectionName->SectionFileName.Buffer, ModuleName.Buffer, ModuleName.Length);
            SectionName->SectionFileName.Buffer[ModuleName.Length / sizeof(WCHAR)] = UNICODE_NULL;
        }

        if (ReturnLength != NULL)
            *ReturnLength = sizeof(MEMORY_SECTION_NAME) + ModuleName.Length + sizeof(WCHAR);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ExFreePoolWithTag(ModuleName.Buffer, 'nFmM');
    return Status;
}

NTSTATUS
NTAPI
MmGetSectionImageInformation(
    _In_ PSECTION SectionObject,
    _Out_ PSECTION_IMAGE_INFORMATION ImageInformation)
{
    PMI_SECTION_OBJECT Section = (PMI_SECTION_OBJECT)SectionObject;

    if (!Section->Control->Image)
        return STATUS_SECTION_NOT_IMAGE;

    *ImageInformation = Section->Control->ImageInformation;
    return STATUS_SUCCESS;
}

VOID
NTAPI
MmGetImageInformation(
    _Out_ PSECTION_IMAGE_INFORMATION ImageInformation)
{
    PMI_SECTION_OBJECT Section = PsGetCurrentProcess()->SectionObject;

    ASSERT(Section != NULL && Section->Control->Image);
    *ImageInformation = Section->Control->ImageInformation;
}

static
BOOLEAN
MiCloseUnusedControlArea(
    _In_ PSECTION_OBJECT_POINTERS Pointers,
    _In_ BOOLEAN Image)
{
    PMI_CONTROL_AREA Control;
    BOOLEAN Found;

    MI_RW_ACQUIRE_SHARED(&MiControlLock);
    Found = MiLookupControlArea(Pointers, Image, &Control);
    MI_RW_RELEASE_SHARED(&MiControlLock);

    if (!Found)
        return TRUE;

    if (Control == NULL)
        return TRUE;

    return MiSegmentDereferenceAndClose(Control->Segment);
}

BOOLEAN
NTAPI
MmFlushImageSection(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_ MMFLUSH_TYPE FlushType)
{
    UNREFERENCED_PARAMETER(FlushType);

    return MiCloseUnusedControlArea(SectionObjectPointer, TRUE);
}

BOOLEAN
NTAPI
MmForceSectionClosed(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_ BOOLEAN DelayClose)
{
    BOOLEAN ImageClosed;
    BOOLEAN DataClosed;

    UNREFERENCED_PARAMETER(DelayClose);

    ImageClosed = MiCloseUnusedControlArea(SectionObjectPointer, TRUE);
    DataClosed = MiCloseUnusedControlArea(SectionObjectPointer, FALSE);
    return (BOOLEAN)(ImageClosed && DataClosed);
}

BOOLEAN
NTAPI
MmDisableModifiedWriteOfSection(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer)
{
    UNREFERENCED_PARAMETER(SectionObjectPointer);
    return FALSE;
}

NTSTATUS
NTAPI
NtCreateSection(
    _Out_ PHANDLE SectionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PLARGE_INTEGER MaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_opt_ HANDLE FileHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER SafeMaximumSize;
    PVOID SectionObject;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    SafeMaximumSize.QuadPart = 0;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWriteHandle(SectionHandle);
            if (MaximumSize != NULL)
                ProbeForRead(MaximumSize, sizeof(LARGE_INTEGER), sizeof(ULONG));
        }

        if (MaximumSize != NULL)
            SafeMaximumSize = *MaximumSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (SafeMaximumSize.QuadPart < 0)
        return STATUS_SECTION_TOO_BIG;

    if (PreviousMode != KernelMode && !(AllocationAttributes & SEC_IMAGE) &&
        (SectionPageProtection & PAGE_IS_EXECUTABLE) && MiDynamicCodeBlocked(PsGetCurrentProcess()))
    {
        return STATUS_DYNAMIC_CODE_BLOCKED;
    }

    Status = MmCreateSection(&SectionObject, DesiredAccess, ObjectAttributes, &SafeMaximumSize,
                             SectionPageProtection, AllocationAttributes, FileHandle, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObInsertObject(SectionObject, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *SectionHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtOpenSection(
    _Out_ PHANDLE SectionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForWriteHandle(SectionHandle);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = ObOpenObjectByName(ObjectAttributes, MmSectionObjectType, PreviousMode, NULL, DesiredAccess, NULL,
                                &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *SectionHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtMapViewOfSection(
    _In_ HANDLE SectionHandle,
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _In_ SIZE_T CommitSize,
    _Inout_opt_ PLARGE_INTEGER SectionOffset,
    _Inout_ PSIZE_T ViewSize,
    _In_ SECTION_INHERIT InheritDisposition,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PMI_SECTION_OBJECT Section;
    LARGE_INTEGER SafeOffset;
    ACCESS_MASK DesiredAccess;
    PVOID SafeBase;
    SIZE_T SafeSize;
    PEPROCESS Process;
    ULONG Protection;
    NTSTATUS Status;

    PAGED_CODE();

    if (AllocationType & ~(MEM_TOP_DOWN | MEM_LARGE_PAGES | MEM_DOS_LIM | SEC_NO_CHANGE | MEM_RESERVE))
        return STATUS_INVALID_PARAMETER;

    if (!MiProtectionFromWin32(Protect, &Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    if (InheritDisposition != ViewShare && InheritDisposition != ViewUnmap)
        return STATUS_INVALID_PARAMETER_8;

    SafeOffset.QuadPart = 0;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWritePointer(BaseAddress);
            ProbeForWriteSize_t(ViewSize);
            if (SectionOffset != NULL)
                ProbeForWriteLargeInteger(SectionOffset);
        }

        SafeBase = *BaseAddress;
        SafeSize = *ViewSize;
        if (SectionOffset != NULL)
            SafeOffset = *SectionOffset;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if ((ULONG_PTR)SafeBase > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        return STATUS_INVALID_PARAMETER_3;

    if ((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS - (ULONG_PTR)SafeBase < SafeSize)
        return STATUS_INVALID_PARAMETER;

    if (ZeroBits > 21 && ZeroBits < 32)
        return STATUS_INVALID_PARAMETER_4;

    if (SafeBase != NULL && ZeroBits != 0 &&
        ((ZeroBits < 32) ? (((ULONG64)(ULONG_PTR)SafeBase >> (32 - ZeroBits)) != 0)
                         : (((ULONG_PTR)SafeBase & ~ZeroBits) != 0)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ZeroBits >= 32 && (ULONG64)ZeroBits - (ULONG64)(ULONG_PTR)SafeBase < SafeSize)
        return STATUS_INVALID_PARAMETER;

    DesiredAccess = MiSectionAccessForProtection[Protection & MI_PROT_ACCESS_MASK];

    Status = ObReferenceObjectByHandle(ProcessHandle, PROCESS_VM_OPERATION, PsProcessType, PreviousMode,
                                       (PVOID *)&Process, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObReferenceObjectByHandle(SectionHandle, DesiredAccess, MmSectionObjectType, PreviousMode,
                                       (PVOID *)&Section, NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return Status;
    }

    if (PreviousMode != KernelMode && (Protect & PAGE_IS_EXECUTABLE) && !Section->Control->Image &&
        MiDynamicCodeBlocked(Process))
    {
        Status = STATUS_DYNAMIC_CODE_BLOCKED;
    }
    else if (!Section->Control->Physical && !(AllocationType & MEM_DOS_LIM) &&
             (((ULONG_PTR)SafeBase | (ULONG64)SafeOffset.QuadPart) & (MI_ALLOCATION_GRANULARITY - 1)))
    {
        Status = STATUS_MAPPED_ALIGNMENT;
    }
    else if ((AllocationType & MEM_RESERVE) && Section->Control->FileObject == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if ((AllocationType & MEM_RESERVE) && !MI_PROT_IS_WRITABLE(Section->Protection))
    {
        Status = STATUS_SECTION_PROTECTION;
    }
    else if ((AllocationType & (MEM_RESERVE | MEM_LARGE_PAGES)) == (MEM_RESERVE | MEM_LARGE_PAGES))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (!Section->Control->Image && !(AllocationType & MEM_RESERVE) &&
             CommitSize > (SafeSize ? SafeSize :
                           (ULONG64)Section->SizeOfSection.QuadPart - (ULONG64)SafeOffset.QuadPart))
    {
        Status = STATUS_INVALID_PARAMETER_5;
    }
    else
    {
        Status = MmMapViewOfSection(Section, Process, &SafeBase, ZeroBits, CommitSize, &SafeOffset, &SafeSize,
                                    InheritDisposition, AllocationType, Protect);
        if (NT_SUCCESS(Status) && Section->Control->Image && !Section->Control->Image64 &&
            sizeof(ULONG_PTR) == sizeof(ULONG64) && PsGetProcessWow64Process(Process) == NULL)
        {
            Status = STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
        }

        if (NT_SUCCESS(Status) && Section->Control->Image && (Process == PsGetCurrentProcess()))
        {
            DbgkMapViewOfSection(Section, SafeBase, SafeOffset.LowPart, SafeSize);
        }
    }

    ObDereferenceObject(Section);
    ObDereferenceObject(Process);

    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *BaseAddress = SafeBase;
        *ViewSize = SafeSize;
        if (SectionOffset != NULL)
            *SectionOffset = SafeOffset;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtUnmapViewOfSection(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode && (ULONG_PTR)BaseAddress > (ULONG_PTR)MmHighestUserAddress)
        return STATUS_NOT_MAPPED_VIEW;

    Status = ObReferenceObjectByHandle(ProcessHandle, PROCESS_VM_OPERATION, PsProcessType, PreviousMode,
                                       (PVOID *)&Process, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (MiProcessHasSecureRanges(Process))
    {
        ULONG64 Start, End;

        MiVadRangeForAddress(MiSpaceOfProcess(Process), (ULONG64)(ULONG_PTR)BaseAddress, &Start, &End);
        if (MiSecureRangeConflict(Process, Start, End, TRUE, 0))
        {
            ObDereferenceObject(Process);
            return STATUS_UNABLE_TO_FREE_VM;
        }
    }

    if (Process == PsGetCurrentProcess())
    {
        DbgkUnMapViewOfSection(BaseAddress);
    }

    Status = MmUnmapViewOfSection(Process, BaseAddress);
    ObDereferenceObject(Process);
    return Status;
}

NTSTATUS
NTAPI
NtExtendSection(
    _In_ HANDLE SectionHandle,
    _Inout_ PLARGE_INTEGER NewMaximumSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PMI_SECTION_OBJECT Section;
    LARGE_INTEGER SafeSize = {0};
    NTSTATUS Status;

    PAGED_CODE();

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForWriteLargeInteger(NewMaximumSize);

        SafeSize = *NewMaximumSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = ObReferenceObjectByHandle(SectionHandle, SECTION_EXTEND_SIZE, MmSectionObjectType, PreviousMode,
                                       (PVOID *)&Section, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Section->Control->Image || Section->Control->FileObject == NULL)
    {
        Status = STATUS_SECTION_NOT_EXTENDED;
    }
    else if (SafeSize.QuadPart > Section->SizeOfSection.QuadPart)
    {
        LARGE_INTEGER FileSize;

        Status = FsRtlGetFileSize(Section->Control->FileObject, &FileSize);
        if (NT_SUCCESS(Status) && SafeSize.QuadPart > FileSize.QuadPart &&
            !MI_PROT_IS_WRITABLE(Section->Protection))
        {
            Status = STATUS_SECTION_NOT_EXTENDED;
        }
        else if (NT_SUCCESS(Status) && SafeSize.QuadPart > FileSize.QuadPart)
        {
            FILE_END_OF_FILE_INFORMATION EndOfFile;

            EndOfFile.EndOfFile = SafeSize;
            Status = IoSetInformation(Section->Control->FileObject, FileEndOfFileInformation, sizeof(EndOfFile),
                                      &EndOfFile);
        }

        if (NT_SUCCESS(Status))
            Status = MiSegmentExtend(Section->Control->Segment, (ULONG64)SafeSize.QuadPart);

        if (NT_SUCCESS(Status))
            Section->SizeOfSection = SafeSize;
    }

    SafeSize = Section->SizeOfSection;
    ObDereferenceObject(Section);

    _SEH2_TRY
    {
        *NewMaximumSize = SafeSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtQuerySection(
    _In_ HANDLE SectionHandle,
    _In_ SECTION_INFORMATION_CLASS SectionInformationClass,
    _Out_ PVOID SectionInformation,
    _In_ SIZE_T SectionInformationLength,
    _Out_opt_ PSIZE_T ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PMI_SECTION_OBJECT Section;
    SIZE_T Needed;
    NTSTATUS Status;

    PAGED_CODE();

    if (SectionInformationClass == SectionBasicInformation)
        Needed = sizeof(SECTION_BASIC_INFORMATION);
    else if (SectionInformationClass == SectionImageInformation)
        Needed = sizeof(SECTION_IMAGE_INFORMATION);
    else
        return STATUS_INVALID_INFO_CLASS;

    if (SectionInformationLength < Needed)
        return STATUS_INFO_LENGTH_MISMATCH;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(SectionInformation, SectionInformationLength, sizeof(ULONG));
            if (ResultLength != NULL)
                ProbeForWriteSize_t(ResultLength);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = ObReferenceObjectByHandle(SectionHandle, SECTION_QUERY, MmSectionObjectType, PreviousMode,
                                       (PVOID *)&Section, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        if (SectionInformationClass == SectionBasicInformation)
        {
            PSECTION_BASIC_INFORMATION Basic = SectionInformation;

            Basic->BaseAddress = Section->BasedAddress;
            Basic->Attributes = Section->AllocationAttributes;
            Basic->Size = Section->SizeOfSection;

            if (Section->Control->FileObject != NULL)
                Basic->Attributes = (Basic->Attributes & ~(SEC_COMMIT | SEC_RESERVE)) | SEC_FILE;
        }
        else if (!Section->Control->Image)
        {
            Status = STATUS_SECTION_NOT_IMAGE;
        }
        else
        {
            *(PSECTION_IMAGE_INFORMATION)SectionInformation = Section->Control->ImageInformation;
        }

        if (NT_SUCCESS(Status) && ResultLength != NULL)
            *ResultLength = Needed;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(Section);
    return Status;
}

NTSTATUS
NTAPI
NtAreMappedFilesTheSame(
    _In_ PVOID File1MappedAsAnImage,
    _In_ PVOID File2MappedAsFile)
{
    PMI_ADDRESS_SPACE Space = MiSpaceOfProcess(PsGetCurrentProcess());
    PMI_CONTROL_AREA First = MiReferenceControlForAddress(Space, File1MappedAsAnImage);
    PMI_CONTROL_AREA Second = MiReferenceControlForAddress(Space, File2MappedAsFile);
    NTSTATUS Status;

    if (First == NULL || Second == NULL)
    {
        Status = (First == NULL) ? STATUS_INVALID_ADDRESS : STATUS_CONFLICTING_ADDRESSES;
    }
    else if (!First->Image)
    {
        Status = STATUS_NOT_SAME_DEVICE;
    }
    else
    {
        Status = (First->FileObject->SectionObjectPointer == Second->FileObject->SectionObjectPointer)
                     ? STATUS_SUCCESS
                     : STATUS_NOT_SAME_DEVICE;
    }

    if (First != NULL)
        MiDereferenceControlArea(First);

    if (Second != NULL)
        MiDereferenceControlArea(Second);

    return Status;
}
