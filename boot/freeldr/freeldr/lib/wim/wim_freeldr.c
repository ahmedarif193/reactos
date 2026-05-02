/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     WIM loader adapter over the shared libwim reader core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include <debug.h>
#include <ramdisk_fatwrite.h>
#include <wim.h>

DBG_DEFAULT_CHANNEL(DISK);

#define TAG_WIM_CORE 'CmiW'
#define TAG_WIM_PATH 'PmiW'

#define WIM_PATH_MAX 1024

typedef struct _WIM_EXTRACT_STATE
{
    PWIM_LOADER_CTX              Ctx;
    PFAT32_WRITER                Writer;
    PWIM_LOADER_PROGRESS_ROUTINE ProgressRoutine;
    PVOID                        ProgressContext;
    PCSTR                        CurrentPath;
    ULONG                        Failures;
} WIM_EXTRACT_STATE, *PWIM_EXTRACT_STATE;

static
PVOID
WimCoreAlloc(
    _In_ SIZE_T Size,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return FrLdrTempAlloc(Size, TAG_WIM_CORE);
}

static
VOID
WimCoreFree(
    _In_opt_ PVOID Ptr,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    if (Ptr)
        FrLdrTempFree(Ptr, TAG_WIM_CORE);
}

static
const WimBlob*
WimFindBlob(
    _In_ PWIM_LOADER_CTX Ctx,
    _In_reads_bytes_(20) const UCHAR *Sha1)
{
    SIZE_T i;

    if (!Ctx || !Sha1)
        return NULL;

    for (i = 0; i < Ctx->Core.BlobCount; ++i)
    {
        if (memcmp(Ctx->Core.Blobs[i].sha1.hash, Sha1, 20) == 0)
            return &Ctx->Core.Blobs[i];
    }

    return NULL;
}

static
BOOLEAN
WimIsSafeLeafName(
    _In_opt_ PCSTR Name)
{
    if (!Name || !*Name)
        return FALSE;
    if (Name[0] == '/')
        return FALSE;
    if (strchr(Name, '/') || strchr(Name, '\\'))
        return FALSE;
    if (strcmp(Name, ".") == 0 || strcmp(Name, "..") == 0)
        return FALSE;
    return TRUE;
}

static
VOID
WimExtractProgressThunk(
    _In_opt_ PVOID Context,
    _In_ ULONGLONG BytesAdvanced,
    _In_opt_ const CHAR *CurrentPath)
{
    PWIM_EXTRACT_STATE State = (PWIM_EXTRACT_STATE)Context;

    if (!State || !State->ProgressRoutine || BytesAdvanced == 0)
        return;

    State->ProgressRoutine(State->ProgressContext,
                           BytesAdvanced,
                           CurrentPath ? CurrentPath : State->CurrentPath);
}

static
BOOLEAN
WimExtractFile(
    _Inout_ PWIM_EXTRACT_STATE State,
    _In_ PCSTR FullPath,
    _In_ const WimDentry *File)
{
    static const UCHAR ZeroSha1[20] = { 0 };
    BOOLEAN IsEmpty;
    ULONG Size = 0;
    PUCHAR Dest = NULL;

    if (!State || !FullPath || !File)
        return FALSE;

    IsEmpty = (memcmp(File->sha1, ZeroSha1, sizeof(ZeroSha1)) == 0);
    if (!IsEmpty)
    {
        if (File->file_size > MAXULONG)
        {
            WARN("WimLoader: file '%s' is too large (%llu bytes)\n",
                 FullPath,
                 File->file_size);
            State->Failures++;
            return TRUE;
        }

        Size = (ULONG)File->file_size;
    }

    if (!Fat32CreateFileEx(State->Writer, FullPath, Size, &Dest))
    {
        WARN("WimLoader: Fat32CreateFileEx failed for '%s'\n", FullPath);
        State->Failures++;
        return TRUE;
    }

    if (Size == 0)
        return TRUE;

    ASSERT(Dest != NULL);

    State->CurrentPath = FullPath;
    if (wimcore_read_blob_into(&State->Ctx->Core,
                               File->sha1,
                               Dest,
                               Size,
                               WimExtractProgressThunk,
                               State,
                               FullPath) != 0)
    {
        ERR("WimLoader: decompression of '%s' failed\n", FullPath);
        State->Failures++;
    }
    State->CurrentPath = NULL;
    return TRUE;
}

static
BOOLEAN
WimWalkTree(
    _Inout_ PWIM_EXTRACT_STATE State,
    _In_ const WimDentry *Directory,
    _Inout_updates_bytes_(WIM_PATH_MAX) PCHAR PathAccum,
    _In_ SIZE_T PathLen)
{
    SIZE_T i;

    if (!State || !Directory || !PathAccum)
        return FALSE;

    for (i = 0; i < Directory->child_count; ++i)
    {
        const WimDentry* Child = &Directory->children[i];
        SIZE_T NameLen;
        SIZE_T NewLen;

        if (!Child->name_utf8 || !WimIsSafeLeafName(Child->name_utf8))
        {
            WARN("WimLoader: skipping unsafe path component '%s'\n",
                 Child->name_utf8 ? Child->name_utf8 : "<null>");
            continue;
        }

        NameLen = strlen(Child->name_utf8);
        if (PathLen + NameLen + 2 >= WIM_PATH_MAX)
        {
            WARN("WimLoader: path too long, skipping '%s'\n", Child->name_utf8);
            continue;
        }

        memcpy(PathAccum + PathLen, Child->name_utf8, NameLen);
        NewLen = PathLen + NameLen;
        PathAccum[NewLen] = '\0';

        if (Child->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!Fat32CreateDirectory(State->Writer, PathAccum))
            {
                WARN("WimLoader: Fat32CreateDirectory failed for '%s'\n", PathAccum);
                State->Failures++;
            }
            else if (Child->child_count != 0)
            {
                PathAccum[NewLen] = '/';
                PathAccum[NewLen + 1] = '\0';
                if (!WimWalkTree(State, Child, PathAccum, NewLen + 1))
                    return FALSE;
                PathAccum[NewLen] = '\0';
            }
        }
        else
        {
            WimExtractFile(State, PathAccum, Child);
        }

        PathAccum[PathLen] = '\0';
    }

    return TRUE;
}

ARC_STATUS
WimLoaderOpen(
    _In_ const VOID *ImageBase,
    _In_ ULONGLONG ImageSize,
    _Out_ PWIM_LOADER_CTX Ctx)
{
    WimCoreAllocator Allocator;

    if (!Ctx)
        return EINVAL;

    RtlZeroMemory(Ctx, sizeof(*Ctx));

    if (!ImageBase || ImageSize < WIM_HEADER_SIZE)
        return EINVAL;

    Allocator.Alloc = WimCoreAlloc;
    Allocator.Free = WimCoreFree;
    Allocator.Context = NULL;

    if (wimcore_open_memory(&Ctx->Core, &Allocator, ImageBase, ImageSize) != 0)
    {
        WimLoaderClose(Ctx);
        return EBADF;
    }

    if (Ctx->Core.ImageCount > MAXULONG)
    {
        WimLoaderClose(Ctx);
        return ENOMEM;
    }

    Ctx->ImageCount = (ULONG)Ctx->Core.ImageCount;
    Ctx->BootIndex = Ctx->Core.Header.boot_index;

    TRACE("WimLoader: valid header, flags=0x%08lx chunk=%lu images=%lu boot=%lu\n",
          Ctx->Core.Header.flags,
          Ctx->Core.Header.chunk_size ? Ctx->Core.Header.chunk_size : 32768u,
          Ctx->ImageCount,
          Ctx->BootIndex);
    TRACE("WimLoader: blob table loaded (%lu entries)\n", (ULONG)Ctx->Core.BlobCount);
    return ESUCCESS;
}

VOID
WimLoaderClose(
    _Inout_ PWIM_LOADER_CTX Ctx)
{
    if (!Ctx)
        return;

    wimcore_close(&Ctx->Core);
    RtlZeroMemory(Ctx, sizeof(*Ctx));
}

ARC_STATUS
WimLoaderSelectImage(
    _Inout_ PWIM_LOADER_CTX Ctx,
    _In_ ULONG ImageIndex)
{
    ULONGLONG MetadataSize;

    if (!Ctx || ImageIndex == 0 || ImageIndex > Ctx->ImageCount)
        return EINVAL;

    if (wimcore_select_image(&Ctx->Core, ImageIndex) != 0)
        return EBADF;

    Ctx->SelectedImage = ImageIndex;
    MetadataSize = Ctx->Core.Images[ImageIndex - 1].metadata_blob.original_size;
    TRACE("WimLoader: selected image %lu (metadata=%llu bytes)\n",
          ImageIndex,
          MetadataSize);
    return ESUCCESS;
}

ARC_STATUS
WimLoaderReadBlob(
    _In_ PWIM_LOADER_CTX Ctx,
    _In_reads_bytes_(20) const UCHAR *Sha1,
    _Out_writes_bytes_(Size) PUCHAR Dest,
    _In_ ULONGLONG Size)
{
    const WimBlob* Blob;

    if (!Ctx || !Sha1 || !Dest)
        return EINVAL;

    Blob = WimFindBlob(Ctx, Sha1);
    if (!Blob)
        return ENOENT;

    if (Blob->original_size != Size)
        return EINVAL;

    if (Size > MAXULONG_PTR)
        return ENOMEM;

    return (wimcore_read_blob_into(&Ctx->Core,
                                   Sha1,
                                   Dest,
                                   (SIZE_T)Size,
                                   NULL,
                                   NULL,
                                   NULL) == 0) ? ESUCCESS : EBADF;
}

ARC_STATUS
WimLoaderQuerySelectedImageStats(
    _In_ PWIM_LOADER_CTX Ctx,
    _Out_ PWIM_LOADER_IMAGE_STATS Stats)
{
    WimCoreImageStats CoreStats;

    if (!Ctx || !Stats || Ctx->SelectedImage == 0)
        return EINVAL;

    if (wimcore_query_image_stats(&Ctx->Core, Ctx->SelectedImage, &CoreStats) != 0)
        return EBADF;

    Stats->FileBytes = CoreStats.FileBytes;
    Stats->FileCount = CoreStats.FileCount;
    Stats->DirectoryCount = CoreStats.DirectoryCount;

    TRACE("WimLoader: selected image stats files=%lu dirs=%lu bytes=%llu\n",
          Stats->FileCount,
          Stats->DirectoryCount,
          Stats->FileBytes);
    return ESUCCESS;
}

ARC_STATUS
WimLoaderExtractToFat(
    _Inout_ PWIM_LOADER_CTX Ctx,
    _Inout_ PFAT32_WRITER Writer,
    _In_opt_ PWIM_LOADER_PROGRESS_ROUTINE ProgressRoutine,
    _In_opt_ PVOID ProgressContext)
{
    const WimDentry* Root;
    CHAR* PathBuf;
    WIM_EXTRACT_STATE State;
    BOOLEAN Ok;

    if (!Ctx || !Writer || Ctx->SelectedImage == 0)
        return EINVAL;

    Root = wimcore_get_root(&Ctx->Core, Ctx->SelectedImage);
    if (!Root || (Root->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) == 0)
        return EBADF;

    PathBuf = (PCHAR)FrLdrTempAlloc(WIM_PATH_MAX, TAG_WIM_PATH);
    if (!PathBuf)
        return ENOMEM;

    PathBuf[0] = '/';
    PathBuf[1] = '\0';

    State.Ctx = Ctx;
    State.Writer = Writer;
    State.ProgressRoutine = ProgressRoutine;
    State.ProgressContext = ProgressContext;
    State.CurrentPath = NULL;
    State.Failures = 0;

    Ok = WimWalkTree(&State, Root, PathBuf, 1);

    FrLdrTempFree(PathBuf, TAG_WIM_PATH);

    if (!Ok)
        return EBADF;

    if (State.Failures)
    {
        WARN("WimLoader: extraction completed with %lu failures\n",
             State.Failures);
    }

    return ESUCCESS;
}
