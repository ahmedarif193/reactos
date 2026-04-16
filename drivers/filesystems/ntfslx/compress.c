/*
 * LZNT1 decompression for the ntfslx ReactOS driver.
 *
 * NTFS compression uses LZNT1 (sub-block based LZ77 variant).
 * Each compression unit is 2^compression_unit clusters, split into
 * 4096-byte sub-blocks. Each sub-block has a 2-byte header:
 *   bits 0-11:  size of compressed data minus 3
 *   bit 15:     1 = compressed, 0 = uncompressed (literal copy)
 *
 * Within a compressed sub-block, a tag byte controls 8 tokens.
 * Bit 0 in the tag = literal byte, bit 1 = (offset, length) phrase.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NTFSLX_SB_SIZE          0x1000
#define NTFSLX_SB_SIZE_MASK     0x0FFF
#define NTFSLX_SB_IS_COMPRESSED 0x8000

/*
 * Decompress a single LZNT1 sub-block.
 *
 * Src points past the 2-byte header into the compressed data.
 * SrcEnd points to the end of the compressed data for this sub-block.
 * Dst is the output buffer, DstEnd is its limit.
 *
 * Returns the number of bytes written to Dst, or -1 on error.
 */
static LONG
NtfslxDecompressSubBlock(
    _In_reads_bytes_(SrcEnd - Src) const UCHAR *Src,
    _In_ const UCHAR *SrcEnd,
    _Out_writes_bytes_(DstEnd - Dst) PUCHAR Dst,
    _In_ PUCHAR DstEnd)
{
    PUCHAR DstStart = Dst;
    UCHAR Tag;
    LONG TokenIndex;

    while (Src < SrcEnd && Dst < DstEnd)
    {
        Tag = *Src++;

        for (TokenIndex = 0; TokenIndex < 8 && Src < SrcEnd && Dst < DstEnd; TokenIndex++)
        {
            if ((Tag & (1 << TokenIndex)) == 0)
            {
                /* Literal byte */
                *Dst++ = *Src++;
            }
            else
            {
                /*
                 * Phrase token: (offset, length) encoded in 2 bytes.
                 *
                 * The split between offset and length bits depends on
                 * how much output has been produced so far in this
                 * sub-block. The number of offset bits is:
                 *   max(4, highest_set_bit(bytes_produced_so_far))
                 * with length bits = 16 - offset_bits.
                 */
                USHORT Phrase;
                ULONG BytesProduced;
                ULONG OffsetBits;
                ULONG LengthMask;
                ULONG MatchOffset;
                ULONG MatchLength;
                const UCHAR *MatchSrc;
                ULONG I;

                if (Src + 1 >= SrcEnd)
                    return -1;

                Phrase = (USHORT)Src[0] | ((USHORT)Src[1] << 8);
                Src += 2;

                BytesProduced = (ULONG)(Dst - DstStart);
                if (BytesProduced == 0)
                    return -1;

                /* Calculate offset/length bit split */
                OffsetBits = 0;
                {
                    ULONG Tmp = BytesProduced - 1;
                    while (Tmp > 0)
                    {
                        OffsetBits++;
                        Tmp >>= 1;
                    }
                }
                if (OffsetBits < 4)
                    OffsetBits = 4;

                LengthMask = (1U << (16 - OffsetBits)) - 1;
                MatchOffset = (Phrase >> (16 - OffsetBits)) + 1;
                MatchLength = (Phrase & LengthMask) + 3;

                if (MatchOffset > BytesProduced)
                    return -1;

                MatchSrc = Dst - MatchOffset;

                for (I = 0; I < MatchLength && Dst < DstEnd; I++)
                {
                    *Dst++ = MatchSrc[I];
                }
            }
        }
    }

    return (LONG)(Dst - DstStart);
}

/*
 * NtfslxDecompressLznt1 - decompress an LZNT1 compressed buffer
 *
 * Processes a sequence of sub-blocks. Each sub-block has a 2-byte header.
 * A zero header terminates the stream; remaining output is zero-filled.
 *
 * Returns STATUS_SUCCESS on success, STATUS_BAD_COMPRESSION_BUFFER on error.
 */
NTSTATUS
NtfslxDecompressLznt1(
    _In_reads_bytes_(CompressedLength) const VOID *CompressedBuffer,
    _In_ ULONG CompressedLength,
    _Out_writes_bytes_(UncompressedLength) PVOID UncompressedBuffer,
    _In_ ULONG UncompressedLength,
    _Out_ PULONG FinalUncompressedSize)
{
    const UCHAR *Src = (const UCHAR *)CompressedBuffer;
    const UCHAR *SrcEnd = Src + CompressedLength;
    PUCHAR Dst = (PUCHAR)UncompressedBuffer;
    PUCHAR DstEnd = Dst + UncompressedLength;

    *FinalUncompressedSize = 0;

    while (Src + 2 <= SrcEnd && Dst < DstEnd)
    {
        USHORT Header;
        ULONG SbCompressedSize;
        LONG Written;

        Header = (USHORT)Src[0] | ((USHORT)Src[1] << 8);
        Src += 2;

        /* Zero header = end of stream */
        if (Header == 0)
            break;

        SbCompressedSize = (Header & NTFSLX_SB_SIZE_MASK) + 1;

        if (Src + SbCompressedSize > SrcEnd)
            return STATUS_BAD_COMPRESSION_BUFFER;

        if (Header & NTFSLX_SB_IS_COMPRESSED)
        {
            /* Decompress this sub-block */
            ULONG MaxOutput = (ULONG)(DstEnd - Dst);
            if (MaxOutput > NTFSLX_SB_SIZE)
                MaxOutput = NTFSLX_SB_SIZE;

            Written = NtfslxDecompressSubBlock(
                Src, Src + SbCompressedSize,
                Dst, Dst + MaxOutput);

            if (Written < 0)
                return STATUS_BAD_COMPRESSION_BUFFER;

            /* Zero-fill remainder of sub-block if output < 4096 */
            if ((ULONG)Written < MaxOutput)
            {
                RtlZeroMemory(Dst + Written, MaxOutput - Written);
                Written = (LONG)MaxOutput;
            }

            Dst += Written;
        }
        else
        {
            /* Uncompressed sub-block: literal copy */
            ULONG CopyLen = SbCompressedSize;
            if (CopyLen > (ULONG)(DstEnd - Dst))
                CopyLen = (ULONG)(DstEnd - Dst);

            RtlCopyMemory(Dst, Src, CopyLen);
            Dst += CopyLen;
        }

        Src += SbCompressedSize;
    }

    /* Zero-fill remaining output */
    if (Dst < DstEnd)
        RtlZeroMemory(Dst, (ULONG)(DstEnd - Dst));

    *FinalUncompressedSize = (ULONG)(Dst - (PUCHAR)UncompressedBuffer);
    return STATUS_SUCCESS;
}
