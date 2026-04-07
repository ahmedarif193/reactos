/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     WIM loader adapter over the shared libwim reader core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __WIM_H
#define __WIM_H

#include <libwim/wimcore.h>

/* This header relies on freeldr core types (ULONG, ARC_STATUS, PFAT32_WRITER,
 * ...), so consumers must include <freeldr.h> before it. */

typedef struct _WIM_LOADER_CTX
{
    WimCoreCtx Core;
    ULONG      ImageCount;
    ULONG      BootIndex;
    ULONG      SelectedImage;
} WIM_LOADER_CTX, *PWIM_LOADER_CTX;

typedef struct _WIM_LOADER_IMAGE_STATS
{
    ULONGLONG FileBytes;
    ULONG     FileCount;
    ULONG     DirectoryCount;
} WIM_LOADER_IMAGE_STATS, *PWIM_LOADER_IMAGE_STATS;

typedef
VOID
(*PWIM_LOADER_PROGRESS_ROUTINE)(
    _In_opt_ PVOID Context,
    _In_ ULONGLONG BytesAdvanced,
    _In_opt_ PCSTR CurrentPath);

ARC_STATUS
WimLoaderOpen(
    _In_ const VOID *ImageBase,
    _In_ ULONGLONG ImageSize,
    _Out_ PWIM_LOADER_CTX Ctx);

ARC_STATUS
WimLoaderSelectImage(
    _Inout_ PWIM_LOADER_CTX Ctx,
    _In_ ULONG ImageIndex);

VOID
WimLoaderClose(
    _Inout_ PWIM_LOADER_CTX Ctx);

ARC_STATUS
WimLoaderQuerySelectedImageStats(
    _In_ PWIM_LOADER_CTX Ctx,
    _Out_ PWIM_LOADER_IMAGE_STATS Stats);

ARC_STATUS
WimLoaderExtractToFat(
    _Inout_ PWIM_LOADER_CTX Ctx,
    _Inout_ PFAT32_WRITER Writer,
    _In_opt_ PWIM_LOADER_PROGRESS_ROUTINE ProgressRoutine,
    _In_opt_ PVOID ProgressContext);

ARC_STATUS
WimLoaderReadBlob(
    _In_ PWIM_LOADER_CTX Ctx,
    _In_reads_bytes_(20) const UCHAR *Sha1,
    _Out_writes_bytes_(Size) PUCHAR Dest,
    _In_ ULONGLONG Size);

#endif /* __WIM_H */
