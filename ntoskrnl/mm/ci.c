/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Enforce process image signing policy at section creation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>
#include <cache/section/newmm.h>
#include <reactos/ci.h>
#include "catalog.h"
#define NDEBUG
#include <debug.h>

#define TAG_CODE_INTEGRITY 'gImM'

typedef struct _MI_CI_FILE
{
    PFILE_OBJECT File;
    NTSTATUS Status;
} MI_CI_FILE;

void *
CiAllocate(size_t Count, size_t Size)
{
    PVOID Buffer;

    if (!Count || !Size || Count > (SIZE_T)-1 / Size)
        return NULL;
    Buffer = ExAllocatePoolWithTag(PagedPool, Count * Size, TAG_CODE_INTEGRITY);
    if (Buffer)
        RtlZeroMemory(Buffer, Count * Size);
    return Buffer;
}

void
CiFree(void *Allocation)
{
    if (Allocation)
        ExFreePoolWithTag(Allocation, TAG_CODE_INTEGRITY);
}

static int
MiCiReadFile(void *Context, uint64_t Offset, void *Buffer, size_t Length)
{
    MI_CI_FILE *File = Context;
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Position;
    ULONG Adjustment = Offset & (PAGE_SIZE - 1);
    ULONG ReadSize;
    PVOID Bounce;

    if (Length > MAXULONG - 2 * PAGE_SIZE || Offset > MAXLONGLONG - Length)
    {
        File->Status = STATUS_INVALID_PARAMETER;
        return 0;
    }
    Position.QuadPart = Offset - Adjustment;
    ReadSize = ROUND_TO_PAGES(Length + Adjustment);
    Bounce = ExAllocatePoolWithTag(PagedPool, ReadSize, TAG_CODE_INTEGRITY);
    if (!Bounce)
    {
        File->Status = STATUS_INSUFFICIENT_RESOURCES;
        return 0;
    }
    File->Status = MiSimpleRead(File->File, &Position, Bounce, ReadSize, TRUE, &Iosb);
    if (NT_SUCCESS(File->Status))
    {
        if (Iosb.Information < Length + Adjustment)
            File->Status = STATUS_END_OF_FILE;
        else
            RtlCopyMemory(Buffer, (PUCHAR)Bounce + Adjustment, Length);
    }
    ExFreePoolWithTag(Bounce, TAG_CODE_INTEGRITY);
    return NT_SUCCESS(File->Status);
}

NTSTATUS
MiValidateImageSigningPolicy(PFILE_OBJECT FileObject)
{
    ULONG Policy = ReadAcquire(&PsGetCurrentProcess()->SignatureMitigationPolicy);
    MI_CI_FILE File = {FileObject, STATUS_SUCCESS};
    LARGE_INTEGER Size, Time;
    UCHAR Digest[32];
    SIZE_T Low = 0, High = RTL_NUMBER_OF(MiSystemImageCatalog), Middle;
    BOOLEAN Verified = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    if (!(Policy & 9) || ExGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;

    Status = FsRtlGetFileSize(FileObject, &Size);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Size.QuadPart <= 0)
        return STATUS_INVALID_IMAGE_FORMAT;

    /* Catalog entries are sorted by size. Hash only a possible OS image. */
    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if (MiSystemImageCatalog[Middle].Size < (ULONGLONG)Size.QuadPart)
            Low = Middle + 1;
        else
            High = Middle;
    }
    if (Low < RTL_NUMBER_OF(MiSystemImageCatalog) && MiSystemImageCatalog[Low].Size == (ULONGLONG)Size.QuadPart)
    {
        if (!CiHashFile(MiCiReadFile, &File, Size.QuadPart, Digest))
            return NT_SUCCESS(File.Status) ? STATUS_INSUFFICIENT_RESOURCES : File.Status;
        while (Low < RTL_NUMBER_OF(MiSystemImageCatalog) && MiSystemImageCatalog[Low].Size == (ULONGLONG)Size.QuadPart)
        {
            if (RtlCompareMemory(Digest, MiSystemImageCatalog[Low].Hash, sizeof(Digest)) == sizeof(Digest))
            {
                Verified = TRUE;
                break;
            }
            ++Low;
        }
    }
    if (!Verified)
    {
        KeQuerySystemTime(&Time);
        Verified = CiVerifyMicrosoftImage(MiCiReadFile, &File, Size.QuadPart, Time.QuadPart / 10000000ULL - 11644473600ULL);
    }
    if (!NT_SUCCESS(File.Status))
        return File.Status;
    if (Verified)
        return STATUS_SUCCESS;

    DPRINT1("Code integrity: %s unsigned or invalid image %wZ (process %p)\n", Policy & 1 ? "blocked" : "audit", &FileObject->FileName, PsGetCurrentProcessId());
    return Policy & 1 ? STATUS_INVALID_IMAGE_HASH : STATUS_SUCCESS;
}
