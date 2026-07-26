/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     WOF/Compact OS XPRESS and LZX data reader
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_WOF_CURRENT_VERSION 1
#define NTFS_WOF_FILE_PROVIDER_VERSION 1
#define NTFS_WOF_REPARSE_DATA_LENGTH 16
#define NTFS_WOF_XPRESS_SYMBOLS 512
#define NTFS_WOF_XPRESS_CODE_LENGTH_BYTES 256
#define NTFS_WOF_XPRESS_MAX_CODE_LENGTH 15
#define NTFS_WOF_XPRESS_MIN_MATCH 3
#define NTFS_WOF_LZX_MAIN_SYMBOLS (256 + 30 * 8)
#define NTFS_WOF_LZX_LENGTH_SYMBOLS 249
#define NTFS_WOF_LZX_PRECODE_SYMBOLS 20
#define NTFS_WOF_LZX_ALIGNED_SYMBOLS 8
#define NTFS_WOF_LZX_MAX_CODE_LENGTH 16
#define NTFS_WOF_HUFFMAN_MAX_SYMBOLS 512
#define NTFS_WOF_HUFFMAN_LENGTHS 17
#define NTFS_WOF_LZX_DEFAULT_BLOCK_SIZE 32768
#define NTFS_WOF_LZX_DEFAULT_FILE_SIZE 12000000

typedef struct _NTFS_WOF_REPARSE_BUFFER
{
    ReparsePointEx Header;
    ULONG WofVersion;
    ULONG WofProvider;
    ULONG ProviderVersion;
    ULONG CompressionFormat;
} NTFS_WOF_REPARSE_BUFFER;

static_assert(sizeof(NTFS_WOF_REPARSE_BUFFER) == 24,
              "WOF reparse layout changed");

typedef struct _NTFS_WOF_BIT_STREAM
{
    const UCHAR* Next;
    const UCHAR* End;
    ULONG BitBuffer;
    ULONG BitsLeft;
    ULONG PaddingUnits;
    BOOLEAN Failed;
} NTFS_WOF_BIT_STREAM, *PNTFS_WOF_BIT_STREAM;

typedef struct _NTFS_WOF_HUFFMAN
{
    ULONG FirstCode[NTFS_WOF_HUFFMAN_LENGTHS];
    USHORT FirstSymbol[NTFS_WOF_HUFFMAN_LENGTHS];
    USHORT Counts[NTFS_WOF_HUFFMAN_LENGTHS];
    USHORT SortedSymbols[NTFS_WOF_HUFFMAN_MAX_SYMBOLS];
    PUSHORT FastTable;
    ULONG FastBits;
    ULONG MaximumBits;
    BOOLEAN Empty;
} NTFS_WOF_HUFFMAN, *PNTFS_WOF_HUFFMAN;

typedef struct _NTFS_WOF_DECOMPRESS_WORKSPACE
{
    NTFS_WOF_HUFFMAN XpressCode;
    NTFS_WOF_HUFFMAN MainCode;
    NTFS_WOF_HUFFMAN LengthCode;
    NTFS_WOF_HUFFMAN PreCode;
    NTFS_WOF_HUFFMAN AlignedCode;
    USHORT XpressFast[1 << 12];
    USHORT MainFast[1 << 11];
    USHORT LengthFast[1 << 10];
    USHORT PreFast[1 << 6];
    USHORT AlignedFast[1 << 7];
    UCHAR XpressLengths[NTFS_WOF_XPRESS_SYMBOLS];
    UCHAR MainLengths[NTFS_WOF_LZX_MAIN_SYMBOLS];
    UCHAR LengthLengths[NTFS_WOF_LZX_LENGTH_SYMBOLS];
    UCHAR PreLengths[NTFS_WOF_LZX_PRECODE_SYMBOLS];
    UCHAR AlignedLengths[NTFS_WOF_LZX_ALIGNED_SYMBOLS];
} NTFS_WOF_DECOMPRESS_WORKSPACE, *PNTFS_WOF_DECOMPRESS_WORKSPACE;

static const UCHAR NtfsWofLzxExtraOffsetBits[30] =
{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
    4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static const ULONG NtfsWofLzxOffsetSlotBase[31] =
{
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24,
    32, 48, 64, 96, 128, 192, 256, 384, 512, 768,
    1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288,
    16384, 24576, 32768
};

static USHORT
NtfsWofReadLe16(_In_reads_bytes_(2) const UCHAR* Buffer)
{
    return (USHORT)Buffer[0] | ((USHORT)Buffer[1] << 8);
}

static ULONG
NtfsWofReadLe32(_In_reads_bytes_(4) const UCHAR* Buffer)
{
    return (ULONG)Buffer[0] |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

static ULONGLONG
NtfsWofReadLe64(_In_reads_bytes_(8) const UCHAR* Buffer)
{
    return (ULONGLONG)NtfsWofReadLe32(Buffer) |
           ((ULONGLONG)NtfsWofReadLe32(Buffer + 4) << 32);
}

static void
NtfsWofWriteLe32(_Out_ UCHAR* Buffer,
                 _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
}

static void
NtfsWofInitializeBitStream(_Out_ PNTFS_WOF_BIT_STREAM Stream,
                           _In_reads_bytes_(Size) const UCHAR* Buffer,
                           _In_ ULONG Size)
{
    Stream->Next = Buffer;
    Stream->End = Buffer + Size;
    Stream->BitBuffer = 0;
    Stream->BitsLeft = 0;
    Stream->PaddingUnits = 0;
    Stream->Failed = FALSE;
}

static void
NtfsWofEnsureBits(_Inout_ PNTFS_WOF_BIT_STREAM Stream,
                  _In_ ULONG Count)
{
    if (Stream->Failed || Count > 16 || Stream->BitsLeft >= Count)
        return;

    if ((SIZE_T)(Stream->End - Stream->Next) < sizeof(USHORT))
    {
        /*
         * XPRESS and LZX omit an all-zero final coding unit. Permit exactly
         * one implicit unit; output-size and match bounds still make a
         * truncated stream fail deterministically.
         */
        if (Stream->PaddingUnits != 0)
        {
            Stream->Failed = TRUE;
            return;
        }
        Stream->PaddingUnits++;
        Stream->BitsLeft += 16;
        return;
    }

    Stream->BitBuffer |=
        (ULONG)NtfsWofReadLe16(Stream->Next) <<
        (16 - Stream->BitsLeft);
    Stream->Next += sizeof(USHORT);
    Stream->BitsLeft += 16;
}

static ULONG
NtfsWofPeekBits(_In_ PNTFS_WOF_BIT_STREAM Stream,
                _In_ ULONG Count)
{
    if (Count == 0)
        return 0;
    return Stream->BitBuffer >> (32 - Count);
}

static void
NtfsWofRemoveBits(_Inout_ PNTFS_WOF_BIT_STREAM Stream,
                  _In_ ULONG Count)
{
    if (Stream->Failed || Count > Stream->BitsLeft)
    {
        Stream->Failed = TRUE;
        return;
    }

    Stream->BitBuffer <<= Count;
    Stream->BitsLeft -= Count;
}

static ULONG
NtfsWofReadBits(_Inout_ PNTFS_WOF_BIT_STREAM Stream,
                _In_ ULONG Count)
{
    ULONG Value;

    if (Count == 0)
        return 0;
    NtfsWofEnsureBits(Stream, Count);
    if (Stream->Failed)
        return 0;
    Value = NtfsWofPeekBits(Stream, Count);
    NtfsWofRemoveBits(Stream, Count);
    return Value;
}

static UCHAR
NtfsWofReadByte(_Inout_ PNTFS_WOF_BIT_STREAM Stream)
{
    if (Stream->Next >= Stream->End)
    {
        Stream->Failed = TRUE;
        return 0;
    }
    return *Stream->Next++;
}

static USHORT
NtfsWofReadU16(_Inout_ PNTFS_WOF_BIT_STREAM Stream)
{
    USHORT Value;

    if ((SIZE_T)(Stream->End - Stream->Next) < sizeof(USHORT))
    {
        Stream->Failed = TRUE;
        return 0;
    }
    Value = NtfsWofReadLe16(Stream->Next);
    Stream->Next += sizeof(USHORT);
    return Value;
}

static ULONG
NtfsWofReadU32(_Inout_ PNTFS_WOF_BIT_STREAM Stream)
{
    ULONG Value;

    if ((SIZE_T)(Stream->End - Stream->Next) < sizeof(ULONG))
    {
        Stream->Failed = TRUE;
        return 0;
    }
    Value = NtfsWofReadLe32(Stream->Next);
    Stream->Next += sizeof(ULONG);
    return Value;
}

static void
NtfsWofAlignBitStream(_Inout_ PNTFS_WOF_BIT_STREAM Stream)
{
    Stream->BitBuffer = 0;
    Stream->BitsLeft = 0;
}

static NTSTATUS
NtfsWofBuildHuffman(
    _Out_ PNTFS_WOF_HUFFMAN Decoder,
    _In_reads_(SymbolCount) const UCHAR* Lengths,
    _In_ ULONG SymbolCount,
    _In_ ULONG MaximumBits,
    _In_ ULONG FastBits,
    _Out_ PUSHORT FastTable)
{
    ULONG Code;
    ULONG Length;
    LONG Remaining;
    ULONG Symbol;
    ULONG TotalSymbols = 0;
    USHORT Offsets[NTFS_WOF_HUFFMAN_LENGTHS];
    ULONG NextCode[NTFS_WOF_HUFFMAN_LENGTHS];

    if (!Decoder ||
        !Lengths ||
        SymbolCount > NTFS_WOF_HUFFMAN_MAX_SYMBOLS ||
        MaximumBits >= NTFS_WOF_HUFFMAN_LENGTHS ||
        FastBits == 0 ||
        FastBits > MaximumBits)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Decoder, sizeof(*Decoder));
    Decoder->FastTable = FastTable;
    Decoder->FastBits = FastBits;
    Decoder->MaximumBits = MaximumBits;
    RtlZeroMemory(FastTable,
                  (SIZE_T)(1UL << FastBits) * sizeof(USHORT));

    for (Symbol = 0; Symbol < SymbolCount; Symbol++)
    {
        Length = Lengths[Symbol];
        if (Length > MaximumBits)
            return STATUS_FILE_CORRUPT_ERROR;
        if (Length != 0)
        {
            Decoder->Counts[Length]++;
            TotalSymbols++;
        }
    }

    if (TotalSymbols == 0)
    {
        Decoder->Empty = TRUE;
        return STATUS_SUCCESS;
    }

    /*
     * A nonempty canonical code must exactly fill its code space.  Reject
     * both oversubscribed and incomplete tables before constructing lookup
     * entries; otherwise a malformed stream could rely on an implicit code.
     */
    Remaining = 1;
    for (Length = 1; Length <= MaximumBits; Length++)
    {
        Remaining = (Remaining << 1) - Decoder->Counts[Length];
        if (Remaining < 0)
            return STATUS_FILE_CORRUPT_ERROR;
    }
    if (Remaining != 0)
        return STATUS_FILE_CORRUPT_ERROR;

    Code = 0;
    Decoder->FirstSymbol[0] = 0;
    for (Length = 1; Length <= MaximumBits; Length++)
    {
        Code = (Code + Decoder->Counts[Length - 1]) << 1;
        if (Code + Decoder->Counts[Length] > (1UL << Length))
            return STATUS_FILE_CORRUPT_ERROR;

        Decoder->FirstCode[Length] = Code;
        Decoder->FirstSymbol[Length] =
            Decoder->FirstSymbol[Length - 1] +
            Decoder->Counts[Length - 1];
        Offsets[Length] = Decoder->FirstSymbol[Length];
        NextCode[Length] = Code;
    }

    for (Symbol = 0; Symbol < SymbolCount; Symbol++)
    {
        Length = Lengths[Symbol];
        if (Length != 0)
            Decoder->SortedSymbols[Offsets[Length]++] = (USHORT)Symbol;
    }

    for (Symbol = 0; Symbol < SymbolCount; Symbol++)
    {
        ULONG Prefix;
        ULONG FillCount;
        USHORT Entry;

        Length = Lengths[Symbol];
        if (Length == 0)
            continue;

        Code = NextCode[Length]++;
        if (Length > FastBits)
            continue;

        Prefix = Code << (FastBits - Length);
        FillCount = 1UL << (FastBits - Length);
        Entry = (USHORT)((Length << 11) | Symbol);
        for (ULONG Index = 0; Index < FillCount; Index++)
            FastTable[Prefix + Index] = Entry;
    }

    return STATUS_SUCCESS;
}

static ULONG
NtfsWofDecodeHuffman(_In_ PNTFS_WOF_HUFFMAN Decoder,
                     _Inout_ PNTFS_WOF_BIT_STREAM Stream)
{
    USHORT Entry;
    ULONG Code = 0;

    if (Decoder->Empty)
    {
        Stream->Failed = TRUE;
        return 0;
    }

    NtfsWofEnsureBits(Stream, Decoder->FastBits);
    if (Stream->Failed)
        return 0;

    Entry = Decoder->FastTable[
        NtfsWofPeekBits(Stream, Decoder->FastBits)];
    if (Entry != 0)
    {
        NtfsWofRemoveBits(Stream, Entry >> 11);
        return Entry & 0x7ff;
    }

    for (ULONG Length = 1;
         Length <= Decoder->MaximumBits;
         Length++)
    {
        ULONG Delta;

        Code = (Code << 1) | NtfsWofReadBits(Stream, 1);
        if (Stream->Failed)
            return 0;

        if (Code < Decoder->FirstCode[Length])
            continue;
        Delta = Code - Decoder->FirstCode[Length];
        if (Delta < Decoder->Counts[Length])
        {
            return Decoder->SortedSymbols[
                Decoder->FirstSymbol[Length] + Delta];
        }
    }

    Stream->Failed = TRUE;
    return 0;
}

static void
NtfsWofCopyMatch(_Inout_ PUCHAR Output,
                 _In_ ULONG OutputOffset,
                 _In_ ULONG Length,
                 _In_ ULONG Distance)
{
    if (Distance == 1)
    {
        RtlFillMemory(Output + OutputOffset,
                      Length,
                      Output[OutputOffset - 1]);
        return;
    }

    for (ULONG Index = 0; Index < Length; Index++)
    {
        Output[OutputOffset + Index] =
            Output[OutputOffset + Index - Distance];
    }
}

static NTSTATUS
NtfsWofDecompressXpress(
    _Inout_ PNTFS_WOF_DECOMPRESS_WORKSPACE Workspace,
    _In_reads_bytes_(InputSize) const UCHAR* Input,
    _In_ ULONG InputSize,
    _Out_ PUCHAR Output,
    _In_ ULONG OutputSize)
{
    NTFS_WOF_BIT_STREAM Stream;
    ULONG OutputOffset = 0;
    NTSTATUS Status;

    if (InputSize < NTFS_WOF_XPRESS_CODE_LENGTH_BYTES)
        return STATUS_FILE_CORRUPT_ERROR;

    for (ULONG Index = 0;
         Index < NTFS_WOF_XPRESS_CODE_LENGTH_BYTES;
         Index++)
    {
        Workspace->XpressLengths[Index * 2] = Input[Index] & 0x0f;
        Workspace->XpressLengths[Index * 2 + 1] = Input[Index] >> 4;
    }

    Status = NtfsWofBuildHuffman(
        &Workspace->XpressCode,
        Workspace->XpressLengths,
        NTFS_WOF_XPRESS_SYMBOLS,
        NTFS_WOF_XPRESS_MAX_CODE_LENGTH,
        12,
        Workspace->XpressFast);
    if (!NT_SUCCESS(Status) || Workspace->XpressCode.Empty)
        return STATUS_FILE_CORRUPT_ERROR;

    NtfsWofInitializeBitStream(
        &Stream,
        Input + NTFS_WOF_XPRESS_CODE_LENGTH_BYTES,
        InputSize - NTFS_WOF_XPRESS_CODE_LENGTH_BYTES);

    while (OutputOffset < OutputSize)
    {
        ULONG Symbol = NtfsWofDecodeHuffman(
            &Workspace->XpressCode,
            &Stream);

        if (Stream.Failed)
            return STATUS_FILE_CORRUPT_ERROR;
        if (Symbol < 256)
        {
            Output[OutputOffset++] = (UCHAR)Symbol;
            continue;
        }

        ULONG Length = Symbol & 0x0f;
        ULONG Log2Distance = (Symbol >> 4) & 0x0f;
        ULONG Distance;

        /*
         * XPRESS interleaves extended match lengths with 16-bit bitstream
         * coding units. Advance over the next coding unit even when the
         * distance itself needs fewer bits, so a following literal byte is
         * read from the correct position.
         */
        NtfsWofEnsureBits(&Stream, 16);
        Distance = (1UL << Log2Distance) |
                   NtfsWofPeekBits(&Stream, Log2Distance);
        NtfsWofRemoveBits(&Stream, Log2Distance);

        if (Length == 0x0f)
        {
            Length += NtfsWofReadByte(&Stream);
            if (Length == 0x0f + 0xff)
                Length = NtfsWofReadU16(&Stream);
        }
        Length += NTFS_WOF_XPRESS_MIN_MATCH;

        if (Stream.Failed ||
            Distance == 0 ||
            Distance > OutputOffset ||
            Length > OutputSize - OutputOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        NtfsWofCopyMatch(Output,
                         OutputOffset,
                         Length,
                         Distance);
        OutputOffset += Length;
    }

    return Stream.Failed
        ? STATUS_FILE_CORRUPT_ERROR
        : STATUS_SUCCESS;
}

static NTSTATUS
NtfsWofReadLzxCodeLengths(
    _Inout_ PNTFS_WOF_DECOMPRESS_WORKSPACE Workspace,
    _Inout_ PNTFS_WOF_BIT_STREAM Stream,
    _Inout_ UCHAR* Lengths,
    _In_ ULONG LengthCount)
{
    NTSTATUS Status;
    ULONG Position = 0;

    for (ULONG Index = 0;
         Index < NTFS_WOF_LZX_PRECODE_SYMBOLS;
         Index++)
    {
        Workspace->PreLengths[Index] =
            (UCHAR)NtfsWofReadBits(Stream, 4);
    }
    if (Stream->Failed)
        return STATUS_FILE_CORRUPT_ERROR;

    Status = NtfsWofBuildHuffman(
        &Workspace->PreCode,
        Workspace->PreLengths,
        NTFS_WOF_LZX_PRECODE_SYMBOLS,
        15,
        6,
        Workspace->PreFast);
    if (!NT_SUCCESS(Status) || Workspace->PreCode.Empty)
    {
        DPRINT1("WOF LZX: invalid precode\n");
        return STATUS_FILE_CORRUPT_ERROR;
    }

    while (Position < LengthCount)
    {
        ULONG Symbol = NtfsWofDecodeHuffman(
            &Workspace->PreCode,
            Stream);
        ULONG RunLength = 1;
        UCHAR NewLength;

        if (Stream->Failed)
            return STATUS_FILE_CORRUPT_ERROR;

        if (Symbol < 17)
        {
            LONG Delta = (LONG)Lengths[Position] - (LONG)Symbol;
            if (Delta < 0)
                Delta += 17;
            NewLength = (UCHAR)Delta;
        }
        else if (Symbol == 17)
        {
            RunLength = 4 + NtfsWofReadBits(Stream, 4);
            NewLength = 0;
        }
        else if (Symbol == 18)
        {
            RunLength = 20 + NtfsWofReadBits(Stream, 5);
            NewLength = 0;
        }
        else if (Symbol == 19)
        {
            ULONG DeltaSymbol;
            LONG Delta;

            RunLength = 4 + NtfsWofReadBits(Stream, 1);
            DeltaSymbol = NtfsWofDecodeHuffman(
                &Workspace->PreCode,
                Stream);
            if (Stream->Failed || DeltaSymbol > 17)
                return STATUS_FILE_CORRUPT_ERROR;
            Delta = (LONG)Lengths[Position] - (LONG)DeltaSymbol;
            if (Delta < 0)
                Delta += 17;
            NewLength = (UCHAR)Delta;
        }
        else
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Stream->Failed || RunLength > LengthCount - Position)
        {
            DPRINT1("WOF LZX: invalid code-length run at %u/%u\n",
                    Position,
                    LengthCount);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        while (RunLength--)
            Lengths[Position++] = NewLength;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsWofReadLzxBlockHeader(
    _Inout_ PNTFS_WOF_DECOMPRESS_WORKSPACE Workspace,
    _Inout_ PNTFS_WOF_BIT_STREAM Stream,
    _Out_ PULONG BlockType,
    _Out_ PULONG BlockSize,
    _Inout_ ULONG RecentOffsets[3])
{
    NTSTATUS Status;

    *BlockType = NtfsWofReadBits(Stream, 3);
    if (NtfsWofReadBits(Stream, 1))
    {
        *BlockSize = NTFS_WOF_LZX_DEFAULT_BLOCK_SIZE;
    }
    else
    {
        *BlockSize = NtfsWofReadBits(Stream, 8) << 8;
        *BlockSize |= NtfsWofReadBits(Stream, 8);
    }
    if (Stream->Failed)
        return STATUS_FILE_CORRUPT_ERROR;

    if (*BlockType == 2)
    {
        for (ULONG Index = 0;
             Index < NTFS_WOF_LZX_ALIGNED_SYMBOLS;
             Index++)
        {
            Workspace->AlignedLengths[Index] =
                (UCHAR)NtfsWofReadBits(Stream, 3);
        }
        if (Stream->Failed)
        {
            DPRINT1("WOF LZX: aligned-code lengths exhausted input\n");
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfsWofBuildHuffman(
            &Workspace->AlignedCode,
            Workspace->AlignedLengths,
            NTFS_WOF_LZX_ALIGNED_SYMBOLS,
            7,
            7,
            Workspace->AlignedFast);
        if (!NT_SUCCESS(Status) || Workspace->AlignedCode.Empty)
        {
            DPRINT1("WOF LZX: invalid aligned code\n");
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    if (*BlockType == 1 || *BlockType == 2)
    {
        Status = NtfsWofReadLzxCodeLengths(
            Workspace,
            Stream,
            Workspace->MainLengths,
            256);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = NtfsWofReadLzxCodeLengths(
            Workspace,
            Stream,
            Workspace->MainLengths + 256,
            NTFS_WOF_LZX_MAIN_SYMBOLS - 256);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = NtfsWofBuildHuffman(
            &Workspace->MainCode,
            Workspace->MainLengths,
            NTFS_WOF_LZX_MAIN_SYMBOLS,
            NTFS_WOF_LZX_MAX_CODE_LENGTH,
            11,
            Workspace->MainFast);
        if (!NT_SUCCESS(Status) || Workspace->MainCode.Empty)
        {
            DPRINT1("WOF LZX: invalid main code\n");
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfsWofReadLzxCodeLengths(
            Workspace,
            Stream,
            Workspace->LengthLengths,
            NTFS_WOF_LZX_LENGTH_SYMBOLS);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("WOF LZX: invalid length code\n");
            return Status;
        }
        Status = NtfsWofBuildHuffman(
            &Workspace->LengthCode,
            Workspace->LengthLengths,
            NTFS_WOF_LZX_LENGTH_SYMBOLS,
            NTFS_WOF_LZX_MAX_CODE_LENGTH,
            10,
            Workspace->LengthFast);
        if (!NT_SUCCESS(Status))
            return Status;
        return STATUS_SUCCESS;
    }

    if (*BlockType == 3)
    {
        /*
         * LZX discards the next coding unit when the stream is already
         * aligned, so force one unit into the bit buffer before aligning.
         */
        NtfsWofEnsureBits(Stream, 1);
        NtfsWofAlignBitStream(Stream);
        RecentOffsets[0] = NtfsWofReadU32(Stream);
        RecentOffsets[1] = NtfsWofReadU32(Stream);
        RecentOffsets[2] = NtfsWofReadU32(Stream);
        if (Stream->Failed ||
            RecentOffsets[0] == 0 ||
            RecentOffsets[1] == 0 ||
            RecentOffsets[2] == 0)
        {
            DPRINT1("WOF LZX: invalid uncompressed-block offsets\n");
            return STATUS_FILE_CORRUPT_ERROR;
        }
        return STATUS_SUCCESS;
    }

    return STATUS_FILE_CORRUPT_ERROR;
}

static NTSTATUS
NtfsWofDecompressLzxBlock(
    _Inout_ PNTFS_WOF_DECOMPRESS_WORKSPACE Workspace,
    _Inout_ PNTFS_WOF_BIT_STREAM Stream,
    _In_ ULONG BlockType,
    _Inout_ PUCHAR Output,
    _In_ ULONG OutputOffset,
    _In_ ULONG BlockSize,
    _Inout_ ULONG RecentOffsets[3])
{
    ULONG BlockOffset = 0;

    while (BlockOffset < BlockSize)
    {
        ULONG Symbol = NtfsWofDecodeHuffman(
            &Workspace->MainCode,
            Stream);

        if (Stream->Failed)
        {
            DPRINT1("WOF LZX: main-symbol exhaustion at block offset %u\n",
                    BlockOffset);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (Symbol < 256)
        {
            Output[OutputOffset + BlockOffset++] = (UCHAR)Symbol;
            continue;
        }

        Symbol -= 256;
        ULONG MatchLength = Symbol % 8;
        ULONG OffsetSlot = Symbol / 8;
        ULONG MatchOffset;

        if (OffsetSlot >= 30)
        {
            DPRINT1("WOF LZX: offset slot %u is out of range\n",
                    OffsetSlot);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (MatchLength == 7)
        {
            MatchLength += NtfsWofDecodeHuffman(
                &Workspace->LengthCode,
                Stream);
        }
        MatchLength += 2;

        if (OffsetSlot < 3)
        {
            ULONG Previous = RecentOffsets[OffsetSlot];
            RecentOffsets[OffsetSlot] = RecentOffsets[0];
            RecentOffsets[0] = Previous;
            MatchOffset = Previous;
        }
        else
        {
            ULONG ExtraBits = NtfsWofLzxExtraOffsetBits[OffsetSlot];

            MatchOffset = NtfsWofLzxOffsetSlotBase[OffsetSlot];
            if (BlockType == 2 && ExtraBits >= 3)
            {
                MatchOffset +=
                    NtfsWofReadBits(Stream, ExtraBits - 3) << 3;
                MatchOffset += NtfsWofDecodeHuffman(
                    &Workspace->AlignedCode,
                    Stream);
            }
            else
            {
                MatchOffset += NtfsWofReadBits(Stream, ExtraBits);
            }
            if (MatchOffset < 2)
            {
                DPRINT1("WOF LZX: explicit offset underflow in slot %u\n",
                        OffsetSlot);
                return STATUS_FILE_CORRUPT_ERROR;
            }
            MatchOffset -= 2;

            RecentOffsets[2] = RecentOffsets[1];
            RecentOffsets[1] = RecentOffsets[0];
            RecentOffsets[0] = MatchOffset;
        }

        if (Stream->Failed ||
            MatchOffset == 0 ||
            MatchOffset > OutputOffset + BlockOffset ||
            MatchLength > BlockSize - BlockOffset)
        {
            DPRINT1("WOF LZX: invalid match at %u: offset %u, "
                    "length %u, remaining %u, stream %u\n",
                    OutputOffset + BlockOffset,
                    MatchOffset,
                    MatchLength,
                    BlockSize - BlockOffset,
                    Stream->Failed);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        NtfsWofCopyMatch(Output,
                         OutputOffset + BlockOffset,
                         MatchLength,
                         MatchOffset);
        BlockOffset += MatchLength;
    }

    return STATUS_SUCCESS;
}

static void
NtfsWofPostprocessLzx(_Inout_ PUCHAR Buffer,
                      _In_ ULONG Size)
{
    if (Size <= 10)
        return;

    for (ULONG Index = 0; Index < Size - 6;)
    {
        LONG AbsoluteOffset;
        LONG RelativeOffset;

        if (Buffer[Index] != 0xe8)
        {
            Index++;
            continue;
        }

        AbsoluteOffset = (LONG)NtfsWofReadLe32(Buffer + Index + 1);
        if (AbsoluteOffset >= 0 &&
            AbsoluteOffset < NTFS_WOF_LZX_DEFAULT_FILE_SIZE)
        {
            RelativeOffset = AbsoluteOffset - (LONG)Index;
            NtfsWofWriteLe32(Buffer + Index + 1,
                             (ULONG)RelativeOffset);
        }
        else if (AbsoluteOffset < 0 &&
                 AbsoluteOffset >= -(LONG)Index)
        {
            RelativeOffset =
                AbsoluteOffset + NTFS_WOF_LZX_DEFAULT_FILE_SIZE;
            NtfsWofWriteLe32(Buffer + Index + 1,
                             (ULONG)RelativeOffset);
        }
        Index += 5;
    }
}

static NTSTATUS
NtfsWofDecompressLzx(
    _Inout_ PNTFS_WOF_DECOMPRESS_WORKSPACE Workspace,
    _In_reads_bytes_(InputSize) const UCHAR* Input,
    _In_ ULONG InputSize,
    _Out_ PUCHAR Output,
    _In_ ULONG OutputSize)
{
    NTFS_WOF_BIT_STREAM Stream;
    ULONG RecentOffsets[3] = {1, 1, 1};
    ULONG OutputOffset = 0;
    BOOLEAN NeedsPostprocessing = FALSE;

    RtlZeroMemory(Workspace->MainLengths,
                  sizeof(Workspace->MainLengths));
    RtlZeroMemory(Workspace->LengthLengths,
                  sizeof(Workspace->LengthLengths));
    NtfsWofInitializeBitStream(&Stream, Input, InputSize);

    while (OutputOffset < OutputSize)
    {
        ULONG BlockType;
        ULONG BlockSize;
        NTSTATUS Status = NtfsWofReadLzxBlockHeader(
            Workspace,
            &Stream,
            &BlockType,
            &BlockSize,
            RecentOffsets);

        if (!NT_SUCCESS(Status) ||
            BlockSize == 0 ||
            BlockSize > OutputSize - OutputOffset)
        {
            DPRINT1("WOF LZX: header/size failure at output %u, "
                    "type %u, size %u, status 0x%08x\n",
                    OutputOffset,
                    BlockType,
                    BlockSize,
                    (ULONG)Status);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (BlockType == 3)
        {
            if ((SIZE_T)(Stream.End - Stream.Next) < BlockSize)
                return STATUS_FILE_CORRUPT_ERROR;
            RtlCopyMemory(Output + OutputOffset,
                          Stream.Next,
                          BlockSize);
            Stream.Next += BlockSize;
            if (BlockSize & 1)
                (void)NtfsWofReadByte(&Stream);
            NeedsPostprocessing = TRUE;
        }
        else
        {
            Status = NtfsWofDecompressLzxBlock(
                Workspace,
                &Stream,
                BlockType,
                Output,
                OutputOffset,
                BlockSize,
                RecentOffsets);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("WOF LZX: block decode failure at output %u\n",
                        OutputOffset);
                return Status;
            }
            if (Workspace->MainLengths[0xe8] != 0)
                NeedsPostprocessing = TRUE;
        }
        OutputOffset += BlockSize;
    }

    if (Stream.Failed)
    {
        DPRINT1("WOF LZX: exhausted input after output completion\n");
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (NeedsPostprocessing)
        NtfsWofPostprocessLzx(Output, OutputSize);
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsWofReadAttributeExact(_In_ PFileRecord File,
                          _In_ PAttribute Attribute,
                          _Out_ PUCHAR Buffer,
                          _In_ ULONG Length,
                          _In_ ULONGLONG Offset)
{
    ULONG Remaining = Length;
    NTSTATUS Status = File->CopyData(Attribute,
                                     Buffer,
                                     &Remaining,
                                     Offset);

    if (!NT_SUCCESS(Status) || Remaining != 0)
        return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsWofReadChunkOffset(_In_ PFileRecord File,
                       _In_ PAttribute BackingAttribute,
                       _In_ ULONGLONG EntryIndex,
                       _In_ ULONG EntrySize,
                       _Out_ PULONGLONG Value)
{
    UCHAR Buffer[sizeof(ULONGLONG)];
    ULONGLONG EntryOffset;
    NTSTATUS Status;

    if (EntryIndex > (~(ULONGLONG)0) / EntrySize)
        return STATUS_FILE_CORRUPT_ERROR;
    EntryOffset = EntryIndex * EntrySize;
    Status = NtfsWofReadAttributeExact(File,
                                       BackingAttribute,
                                       Buffer,
                                       EntrySize,
                                       EntryOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    *Value = EntrySize == sizeof(ULONG)
        ? NtfsWofReadLe32(Buffer)
        : NtfsWofReadLe64(Buffer);
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::TryCopyWofCompressedData(
    _In_ PAttribute LogicalAttribute,
    _In_ PUCHAR Buffer,
    _Inout_ PULONG Length,
    _In_ ULONGLONG Offset,
    _Out_ PBOOLEAN Handled)
{
    static WCHAR WofCompressedDataName[] =
        L"WofCompressedData";
    PAttribute ReparseAttribute;
    PAttribute BackingAttribute;
    NTFS_WOF_REPARSE_BUFFER ReparseBuffer;
    PNTFS_WOF_DECOMPRESS_WORKSPACE Workspace = NULL;
    PUCHAR StoredBuffer = NULL;
    PUCHAR ChunkBuffer = NULL;
    ULONGLONG LogicalSize;
    ULONGLONG BackingSize;
    ULONGLONG ChunkCount;
    ULONGLONG TableEntryCount;
    ULONGLONG TableSize;
    ULONGLONG DataRegionSize;
    ULONGLONG ChunkIndex;
    ULONGLONG RelativeStart;
    ULONG TableEntrySize;
    ULONG BytesToRead;
    ULONG BytesRead = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!LogicalAttribute || !Buffer || !Length || !Handled)
        return STATUS_INVALID_PARAMETER;
    *Handled = FALSE;

    if (WofProbeState == 1)
        return STATUS_SUCCESS;

    if (WofProbeState == 0)
    {
        ULONGLONG ReparseSize;

        ReparseAttribute = GetAttribute(TypeReparsePoint, NULL);
        if (!ReparseAttribute)
        {
            WofProbeState = 1;
            return STATUS_SUCCESS;
        }

        ReparseSize = GetAttributeDataSize(ReparseAttribute);
        if (ReparseSize < sizeof(ReparsePointEx))
            return STATUS_IO_REPARSE_DATA_INVALID;

        Status = NtfsWofReadAttributeExact(
            this,
            ReparseAttribute,
            (PUCHAR)&ReparseBuffer.Header,
            sizeof(ReparseBuffer.Header),
            0);
        if (!NT_SUCCESS(Status))
            return Status;
        if (ReparseBuffer.Header.ReparseType !=
            NTFS_IO_REPARSE_TAG_WOF)
        {
            WofProbeState = 1;
            return STATUS_SUCCESS;
        }

        *Handled = TRUE;
        if (ReparseSize != sizeof(ReparseBuffer))
            return STATUS_IO_REPARSE_DATA_INVALID;
        Status = NtfsWofReadAttributeExact(
            this,
            ReparseAttribute,
            (PUCHAR)&ReparseBuffer,
            sizeof(ReparseBuffer),
            0);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = NtfsValidateReparseBuffer(
            (const UCHAR*)&ReparseBuffer,
            sizeof(ReparseBuffer),
            NULL);
        if (!NT_SUCCESS(Status) ||
            ReparseBuffer.Header.ReparseDataLength !=
                NTFS_WOF_REPARSE_DATA_LENGTH ||
            ReparseBuffer.Header.Padding != 0 ||
            ReparseBuffer.WofVersion !=
                NTFS_WOF_CURRENT_VERSION ||
            ReparseBuffer.ProviderVersion !=
                NTFS_WOF_FILE_PROVIDER_VERSION)
        {
            return STATUS_IO_REPARSE_DATA_INVALID;
        }
        if (ReparseBuffer.WofProvider != NTFS_WOF_PROVIDER_FILE)
            return STATUS_IO_REPARSE_TAG_NOT_HANDLED;

        WofAlgorithm = ReparseBuffer.CompressionFormat;
        switch (WofAlgorithm)
        {
            case NTFS_WOF_COMPRESSION_XPRESS4K:
                WofChunkSize = 4 * 1024;
                break;
            case NTFS_WOF_COMPRESSION_LZX:
                WofChunkSize = 32 * 1024;
                break;
            case NTFS_WOF_COMPRESSION_XPRESS8K:
                WofChunkSize = 8 * 1024;
                break;
            case NTFS_WOF_COMPRESSION_XPRESS16K:
                WofChunkSize = 16 * 1024;
                break;
            default:
                return STATUS_IO_REPARSE_TAG_NOT_HANDLED;
        }
        WofProbeState = 2;
    }

    *Handled = TRUE;
    if (*Length == 0)
        return STATUS_SUCCESS;
    if (LogicalAttribute->AttributeType != TypeData ||
        LogicalAttribute->NameLength != 0 ||
        (LogicalAttribute->Flags &
            (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED)))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    LogicalSize = GetAttributeDataSize(LogicalAttribute);
    if (Offset >= LogicalSize)
        return STATUS_END_OF_FILE;
    BytesToRead = (ULONG)min(LogicalSize - Offset,
                             (ULONGLONG)*Length);

    BackingAttribute = GetAttribute(TypeData,
                                    WofCompressedDataName);
    if (!BackingAttribute ||
        (BackingAttribute->Flags &
            (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED)))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    BackingSize = GetAttributeDataSize(BackingAttribute);

    ChunkCount = LogicalSize / WofChunkSize;
    if (LogicalSize % WofChunkSize)
        ChunkCount++;
    if (ChunkCount == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    TableEntryCount = ChunkCount - 1;
    TableEntrySize = LogicalSize < 0x100000000ULL
        ? sizeof(ULONG)
        : sizeof(ULONGLONG);
    if (TableEntryCount >
        (~(ULONGLONG)0) / TableEntrySize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    TableSize = TableEntryCount * TableEntrySize;
    if (TableSize > BackingSize)
        return STATUS_FILE_CORRUPT_ERROR;
    DataRegionSize = BackingSize - TableSize;

    StoredBuffer = new(PagedPool, TAG_NTFS) UCHAR[WofChunkSize];
    ChunkBuffer = new(PagedPool, TAG_NTFS) UCHAR[WofChunkSize];
    if (!StoredBuffer || !ChunkBuffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    ChunkIndex = Offset / WofChunkSize;
    if (ChunkIndex == 0)
    {
        RelativeStart = 0;
    }
    else
    {
        Status = NtfsWofReadChunkOffset(
            this,
            BackingAttribute,
            ChunkIndex - 1,
            TableEntrySize,
            &RelativeStart);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    while (BytesRead < BytesToRead)
    {
        ULONGLONG ChunkStartOffset =
            ChunkIndex * (ULONGLONG)WofChunkSize;
        ULONGLONG RelativeEnd;
        ULONGLONG StoredSize64;
        ULONGLONG DataOffset;
        ULONG UncompressedSize;
        ULONG OffsetInChunk;
        ULONG CopyLength;

        if (ChunkIndex + 1 == ChunkCount)
        {
            RelativeEnd = DataRegionSize;
        }
        else
        {
            Status = NtfsWofReadChunkOffset(
                this,
                BackingAttribute,
                ChunkIndex,
                TableEntrySize,
                &RelativeEnd);
            if (!NT_SUCCESS(Status))
                goto Done;
        }

        if (RelativeEnd <= RelativeStart ||
            RelativeEnd > DataRegionSize)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        StoredSize64 = RelativeEnd - RelativeStart;
        UncompressedSize = (ULONG)min(
            LogicalSize - ChunkStartOffset,
            (ULONGLONG)WofChunkSize);
        if (StoredSize64 > UncompressedSize ||
            StoredSize64 > WofChunkSize ||
            TableSize > (~(ULONGLONG)0) - RelativeStart)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        DataOffset = TableSize + RelativeStart;
        Status = NtfsWofReadAttributeExact(
            this,
            BackingAttribute,
            StoredBuffer,
            (ULONG)StoredSize64,
            DataOffset);
        if (!NT_SUCCESS(Status))
            goto Done;

        if (StoredSize64 == UncompressedSize)
        {
            RtlCopyMemory(ChunkBuffer,
                          StoredBuffer,
                          UncompressedSize);
        }
        else
        {
            if (!Workspace)
            {
                Workspace =
                    new(PagedPool, TAG_NTFS)
                        NTFS_WOF_DECOMPRESS_WORKSPACE;
                if (!Workspace)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Done;
                }
                RtlZeroMemory(Workspace, sizeof(*Workspace));
            }

            if (WofAlgorithm == NTFS_WOF_COMPRESSION_LZX)
            {
                Status = NtfsWofDecompressLzx(
                    Workspace,
                    StoredBuffer,
                    (ULONG)StoredSize64,
                    ChunkBuffer,
                    UncompressedSize);
            }
            else
            {
                Status = NtfsWofDecompressXpress(
                    Workspace,
                    StoredBuffer,
                    (ULONG)StoredSize64,
                    ChunkBuffer,
                    UncompressedSize);
            }
            if (!NT_SUCCESS(Status))
                goto Done;
        }

        OffsetInChunk = (ULONG)(
            Offset + BytesRead - ChunkStartOffset);
        CopyLength = min(BytesToRead - BytesRead,
                         UncompressedSize - OffsetInChunk);
        RtlCopyMemory(Buffer + BytesRead,
                      ChunkBuffer + OffsetInChunk,
                      CopyLength);
        BytesRead += CopyLength;
        RelativeStart = RelativeEnd;
        ChunkIndex++;
    }

    *Length -= BytesRead;

Done:
    delete Workspace;
    delete[] ChunkBuffer;
    delete[] StoredBuffer;
    return Status;
}
