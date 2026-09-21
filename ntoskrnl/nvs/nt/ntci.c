/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntci.c
 * PURPOSE:     Process image signing policy at image section creation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>
#include <reactos/ci.h>
#include "catalog.h"

#define TAG_CODE_INTEGRITY 'gImM'
#define MI_CI_CHUNK (64 * 1024)

typedef struct _MI_CI_FILE
{
    PFILE_OBJECT File;
    PUCHAR Bounce;
    NTSTATUS Status;
} MI_CI_FILE, *PMI_CI_FILE;

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
    PMI_CI_FILE File = Context;
    PUCHAR Output = Buffer;

    if (Length > MAXULONG - 2 * PAGE_SIZE || Offset > MAXLONGLONG - Length)
    {
        File->Status = STATUS_INVALID_PARAMETER;
        return 0;
    }

    while (Length != 0)
    {
        ULONG Adjustment = (ULONG)(Offset & (PAGE_SIZE - 1));
        ULONG Chunk = (ULONG)min(Length, (size_t)(MI_CI_CHUNK - Adjustment));
        ULONG Transferred;

        File->Status = MiPagingIo(File->File, Offset - Adjustment, ROUND_TO_PAGES(Chunk + Adjustment), File->Bounce,
                                  FALSE, &Transferred);
        if (!NT_SUCCESS(File->Status))
            return 0;

        if (Transferred < Chunk + Adjustment)
        {
            File->Status = STATUS_END_OF_FILE;
            return 0;
        }

        RtlCopyMemory(Output, File->Bounce + Adjustment, Chunk);
        Output += Chunk;
        Offset += Chunk;
        Length -= Chunk;
    }

    return 1;
}

NTSTATUS
MiValidateImageSigningPolicy(
    _In_ PFILE_OBJECT FileObject)
{
    ULONG Policy = PsGetProcessMitigationPolicyFlags(PsGetCurrentProcess(), PSP_SIGNATURE_POLICY);
    MI_CI_FILE File = { FileObject, NULL, STATUS_SUCCESS };
    SIZE_T Low = 0, High = RTL_NUMBER_OF(MiSystemImageCatalog), Middle;
    LARGE_INTEGER Size, Time;
    BOOLEAN Verified = FALSE;
    UCHAR Digest[32];
    NTSTATUS Status;

    PAGED_CODE();

    if (!(Policy & 9) || ExGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;

    Status = FsRtlGetFileSize(FileObject, &Size);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Size.QuadPart <= 0)
        return STATUS_INVALID_IMAGE_FORMAT;

    File.Bounce = ExAllocatePoolWithTag(NonPagedPool, MI_CI_CHUNK, TAG_CODE_INTEGRITY);
    if (File.Bounce == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

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
        {
            ExFreePoolWithTag(File.Bounce, TAG_CODE_INTEGRITY);
            return NT_SUCCESS(File.Status) ? STATUS_INSUFFICIENT_RESOURCES : File.Status;
        }

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
        Verified = CiVerifyMicrosoftImage(MiCiReadFile, &File, Size.QuadPart,
                                          Time.QuadPart / 10000000ULL - 11644473600ULL);
    }

    ExFreePoolWithTag(File.Bounce, TAG_CODE_INTEGRITY);

    if (!NT_SUCCESS(File.Status))
        return File.Status;

    if (Verified)
        return STATUS_SUCCESS;

    return (Policy & 1) ? STATUS_INVALID_IMAGE_HASH : STATUS_SUCCESS;
}
