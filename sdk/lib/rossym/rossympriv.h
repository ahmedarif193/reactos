/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            lib/rossym/rossympriv.h
 * PURPOSE:         Private header for rossym
 *
 * PROGRAMMERS:     Ge van Geldorp (gvg@reactos.com)
 */

#pragma once

extern ROSSYM_CALLBACKS RosSymCallbacks;

/* rossym is built for both kernel and user mode, so declare the decompressor
 * rather than pulling in a mode-specific header for it. */
NTSYSAPI
NTSTATUS
NTAPI
RtlDecompressBuffer(
    _In_ USHORT CompressionFormat,
    _Out_writes_bytes_to_(UncompressedBufferSize, *FinalUncompressedSize) PUCHAR UncompressedBuffer,
    _In_ ULONG UncompressedBufferSize,
    _In_reads_bytes_(CompressedBufferSize) PUCHAR CompressedBuffer,
    _In_ ULONG CompressedBufferSize,
    _Out_ PULONG FinalUncompressedSize);

#define RosSymAllocMem(Size) (*RosSymCallbacks.AllocMemProc)(Size)
#define RosSymFreeMem(Area) (*RosSymCallbacks.FreeMemProc)(Area)
#define RosSymReadFile(FileContext, Buffer, Size) (*RosSymCallbacks.ReadFileProc)((FileContext), (Buffer), (Size))
#define RosSymSeekFile(FileContext, Position) (*RosSymCallbacks.SeekFileProc)((FileContext), (Position))

extern BOOLEAN RosSymZwReadFile(PVOID FileContext, PVOID Buffer, ULONG Size);
extern BOOLEAN RosSymZwSeekFile(PVOID FileContext, ULONG_PTR Position);

#define ROSSYM_IS_VALID_DOS_HEADER(DosHeader) (IMAGE_DOS_SIGNATURE == (DosHeader)->e_magic \
                                               && 0L != (DosHeader)->e_lfanew)
#define ROSSYM_IS_VALID_NT_HEADERS(NtHeaders) (IMAGE_NT_SIGNATURE == (NtHeaders)->Signature \
                                               && IMAGE_NT_OPTIONAL_HDR_MAGIC == (NtHeaders)->OptionalHeader.Magic)

/* EOF */
