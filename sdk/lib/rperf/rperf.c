/*
 * PROJECT:     ReactOS system profiling
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Bounded streaming .rperf v2 codec
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <limits.h>
#include <string.h>

#include <reactos/rperf.h>

#define RPERF_WRITER_FINALIZED UINT32_C(0x80000000)

static const uint8_t RperfFileMagic[8] =
{
    'R', 'P', 'E', 'R', 'F', '2', 0, 0
};

static int
RperfAddU64(uint64_t Left, uint64_t Right, uint64_t *Result)
{
    if (Right > UINT64_MAX - Left)
        return 0;
    *Result = Left + Right;
    return 1;
}

static int
RperfMultiplyU32(uint32_t Left, uint32_t Right, uint32_t *Result)
{
    if (Left != 0 && Right > UINT32_MAX / Left)
        return 0;
    *Result = Left * Right;
    return 1;
}

static int
RperfPayloadRangeValid(const uint8_t *Bytes,
                       uint32_t TotalSize,
                       uint32_t HeaderSize,
                       uint32_t Offset,
                       uint32_t ByteCount)
{
    uint32_t End, PaddedEnd, Index;

    if (ByteCount == 0)
        return Offset == 0 && TotalSize == HeaderSize;
    if (Offset != HeaderSize || Offset > TotalSize ||
        ByteCount > TotalSize - Offset)
    {
        return 0;
    }
    End = Offset + ByteCount;
    if (End > UINT32_MAX - (RPERF_ALIGNMENT - 1))
        return 0;
    PaddedEnd = (End + (RPERF_ALIGNMENT - 1)) &
                ~(RPERF_ALIGNMENT - 1);
    if (TotalSize != PaddedEnd)
        return 0;
    for (Index = End; Index < PaddedEnd; Index++)
    {
        if (Bytes[Index] != 0)
            return 0;
    }
    return 1;
}

static uint64_t
RperfAlignUpU64(uint64_t Value)
{
    uint64_t Mask = RPERF_ALIGNMENT - 1;

    if (Value > UINT64_MAX - Mask)
        return 0;
    return (Value + Mask) & ~Mask;
}

uint16_t
RperfLoadLe16(const void *Buffer)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;

    return (uint16_t)((uint16_t)Bytes[0] |
                      ((uint16_t)Bytes[1] << 8));
}

uint32_t
RperfLoadLe32(const void *Buffer)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;

    return (uint32_t)Bytes[0] |
           ((uint32_t)Bytes[1] << 8) |
           ((uint32_t)Bytes[2] << 16) |
           ((uint32_t)Bytes[3] << 24);
}

uint64_t
RperfLoadLe64(const void *Buffer)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;

    return (uint64_t)RperfLoadLe32(Bytes) |
           ((uint64_t)RperfLoadLe32(Bytes + 4) << 32);
}

void
RperfStoreLe16(void *Buffer, uint16_t Value)
{
    uint8_t *Bytes = (uint8_t *)Buffer;

    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
}

void
RperfStoreLe32(void *Buffer, uint32_t Value)
{
    uint8_t *Bytes = (uint8_t *)Buffer;

    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
    Bytes[2] = (uint8_t)(Value >> 16);
    Bytes[3] = (uint8_t)(Value >> 24);
}

void
RperfStoreLe64(void *Buffer, uint64_t Value)
{
    uint8_t *Bytes = (uint8_t *)Buffer;

    RperfStoreLe32(Bytes, (uint32_t)Value);
    RperfStoreLe32(Bytes + 4, (uint32_t)(Value >> 32));
}

uint32_t
RperfCrc32(uint32_t InitialCrc, const void *Buffer, size_t BufferSize)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;
    uint32_t Crc = ~InitialCrc;
    size_t ByteIndex;

    if (Buffer == NULL && BufferSize != 0)
        return 0;

    for (ByteIndex = 0; ByteIndex < BufferSize; ByteIndex++)
    {
        unsigned int Bit;

        Crc ^= Bytes[ByteIndex];
        for (Bit = 0; Bit < 8; Bit++)
            Crc = (Crc >> 1) ^
                  (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(Crc & 1));
    }

    return ~Crc;
}

const char *
RperfStatusString(RPERF_STATUS Status)
{
    switch (Status)
    {
        case RPERF_OK: return "success";
        case RPERF_S_END_OF_FILE: return "end of file";
        case RPERF_S_RECOVERED: return "recovered";
        case RPERF_S_MORE_DATA: return "more data";
        case RPERF_E_INVALID_ARGUMENT: return "invalid argument";
        case RPERF_E_IO: return "I/O error";
        case RPERF_E_TRUNCATED: return "truncated input";
        case RPERF_E_BAD_MAGIC: return "bad magic";
        case RPERF_E_BAD_VERSION: return "unsupported version";
        case RPERF_E_BAD_ENDIAN: return "unsupported byte order";
        case RPERF_E_BAD_SIZE: return "invalid size";
        case RPERF_E_OVERFLOW: return "integer overflow";
        case RPERF_E_CHECKSUM: return "checksum mismatch";
        case RPERF_E_UNSUPPORTED: return "unsupported feature";
        case RPERF_E_STATE: return "invalid codec state";
        case RPERF_E_LIMIT: return "configured limit exceeded";
        case RPERF_E_CORRUPT: return "corrupt input";
        case RPERF_E_NOT_FOUND: return "not found";
        default: return "unknown status";
    }
}

RPERF_STATUS
RperfCalculateRecordSize(uint32_t HeaderSize,
                         uint32_t PayloadBytes,
                         uint32_t *RecordSize)
{
    uint64_t Size;

    if (RecordSize == NULL || HeaderSize < RPERF_RECORD_HEADER_SIZE ||
        (HeaderSize & (RPERF_ALIGNMENT - 1)) != 0)
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    Size = (uint64_t)HeaderSize + PayloadBytes;
    Size = RperfAlignUpU64(Size);
    if (Size == 0 || Size > UINT32_MAX)
        return RPERF_E_OVERFLOW;
    if (Size > RPERF_HARD_MAX_RECORD_BYTES)
        return RPERF_E_LIMIT;
    *RecordSize = (uint32_t)Size;
    return RPERF_OK;
}

static RPERF_STATUS
RperfWriteExact(RPERF_WRITE_AT_CALLBACK WriteAt,
                void *Context,
                uint64_t Offset,
                const void *Buffer,
                uint32_t BufferSize)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;
    uint32_t Done = 0;

    if (Offset > UINT64_MAX - BufferSize)
        return RPERF_E_OVERFLOW;
    while (Done != BufferSize)
    {
        uint32_t Written = 0;
        RPERF_STATUS Status;

        Status = WriteAt(Context, Offset + Done, Bytes + Done, BufferSize - Done, &Written);
        if (Status != RPERF_OK)
            return Status;
        if (Written == 0 || Written > BufferSize - Done)
            return RPERF_E_IO;
        Done += Written;
    }

    return RPERF_OK;
}

static RPERF_STATUS
RperfReadExact(RPERF_READ_AT_CALLBACK ReadAt,
               void *Context,
               uint64_t Offset,
               void *Buffer,
               uint32_t BufferSize)
{
    uint8_t *Bytes = (uint8_t *)Buffer;
    uint32_t Done = 0;

    if (Offset > UINT64_MAX - BufferSize)
        return RPERF_E_OVERFLOW;
    while (Done != BufferSize)
    {
        uint32_t Read = 0;
        RPERF_STATUS Status;

        Status = ReadAt(Context, Offset + Done, Bytes + Done, BufferSize - Done, &Read);
        if (Status != RPERF_OK)
            return Status;
        if (Read == 0)
            return RPERF_E_TRUNCATED;
        if (Read > BufferSize - Done)
            return RPERF_E_IO;
        Done += Read;
    }

    return RPERF_OK;
}

static uint32_t
RperfHeaderCrc(const uint8_t *Bytes,
               uint32_t HeaderSize,
               uint32_t CrcOffset)
{
    static const uint8_t Zeros[4] = {0, 0, 0, 0};
    uint32_t Crc;

    Crc = RperfCrc32(0, Bytes, CrcOffset);
    Crc = RperfCrc32(Crc, Zeros, sizeof(Zeros));
    return RperfCrc32(Crc, Bytes + CrcOffset + sizeof(Zeros), HeaderSize - CrcOffset - sizeof(Zeros));
}

static void
RperfDecodeFileHeader(const uint8_t *Bytes, RPERF_FILE_HEADER_V2 *Header)
{
    unsigned int Index;

    memset(Header, 0, sizeof(*Header));
    memcpy(Header->Magic, Bytes, sizeof(Header->Magic));
    Header->VersionMajor = RperfLoadLe16(Bytes + 8);
    Header->VersionMinor = RperfLoadLe16(Bytes + 10);
    Header->EndianMarker = RperfLoadLe32(Bytes + 12);
    Header->HeaderSize = RperfLoadLe32(Bytes + 16);
    Header->Flags = RperfLoadLe32(Bytes + 20);
    Header->ChecksumAlgorithm = RperfLoadLe32(Bytes + 24);
    Header->CompressionAlgorithm = RperfLoadLe32(Bytes + 28);
    memcpy(Header->SessionId, Bytes + 32, sizeof(Header->SessionId));
    Header->CreatedSystemTime100ns = RperfLoadLe64(Bytes + 48);
    Header->TimestampFrequency = RperfLoadLe64(Bytes + 56);
    Header->FirstChunkOffset = RperfLoadLe64(Bytes + 64);
    Header->FileSize = RperfLoadLe64(Bytes + 72);
    Header->ChunkCount = RperfLoadLe64(Bytes + 80);
    Header->IndexChunkOffset = RperfLoadLe64(Bytes + 88);
    Header->FooterChunkOffset = RperfLoadLe64(Bytes + 96);
    Header->HeaderCrc32 = RperfLoadLe32(Bytes + 104);
    for (Index = 0; Index < 5; Index++)
        Header->Reserved[Index] = RperfLoadLe32(Bytes + 108 + Index * 4);
}

static void
RperfEncodeFileHeader(RPERF_FILE_HEADER_V2 *Header,
                      uint8_t Bytes[RPERF_FILE_HEADER_SIZE])
{
    memset(Bytes, 0, RPERF_FILE_HEADER_SIZE);
    memcpy(Bytes, RperfFileMagic, sizeof(RperfFileMagic));
    RperfStoreLe16(Bytes + 8, Header->VersionMajor);
    RperfStoreLe16(Bytes + 10, Header->VersionMinor);
    RperfStoreLe32(Bytes + 12, Header->EndianMarker);
    RperfStoreLe32(Bytes + 16, Header->HeaderSize);
    RperfStoreLe32(Bytes + 20, Header->Flags);
    RperfStoreLe32(Bytes + 24, Header->ChecksumAlgorithm);
    RperfStoreLe32(Bytes + 28, Header->CompressionAlgorithm);
    memcpy(Bytes + 32, Header->SessionId, sizeof(Header->SessionId));
    RperfStoreLe64(Bytes + 48, Header->CreatedSystemTime100ns);
    RperfStoreLe64(Bytes + 56, Header->TimestampFrequency);
    RperfStoreLe64(Bytes + 64, Header->FirstChunkOffset);
    RperfStoreLe64(Bytes + 72, Header->FileSize);
    RperfStoreLe64(Bytes + 80, Header->ChunkCount);
    RperfStoreLe64(Bytes + 88, Header->IndexChunkOffset);
    RperfStoreLe64(Bytes + 96, Header->FooterChunkOffset);
    Header->HeaderCrc32 = RperfHeaderCrc(Bytes, RPERF_FILE_HEADER_SIZE, 104);
    RperfStoreLe32(Bytes + 104, Header->HeaderCrc32);
}

RPERF_STATUS
RperfValidateFileHeader(const void *Buffer,
                        size_t BufferSize,
                        uint32_t ReaderFlags,
                        RPERF_FILE_HEADER_V2 *Header)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;
    RPERF_FILE_HEADER_V2 Local;
    uint32_t ExpectedCrc;
    unsigned int Index;

    if (Buffer == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (BufferSize < RPERF_FILE_HEADER_SIZE)
        return RPERF_E_TRUNCATED;
    if (memcmp(Bytes, RperfFileMagic, sizeof(RperfFileMagic)) != 0)
        return RPERF_E_BAD_MAGIC;

    RperfDecodeFileHeader(Bytes, &Local);
    if (Local.VersionMajor != RPERF_VERSION_MAJOR)
        return RPERF_E_BAD_VERSION;
    if (Local.VersionMinor > RPERF_VERSION_MINOR &&
        !(ReaderFlags & RPERF_READER_FLAG_ALLOW_NEWER_MINOR))
    {
        return RPERF_E_BAD_VERSION;
    }
    if (Local.EndianMarker != RPERF_ENDIAN_MARKER)
        return RPERF_E_BAD_ENDIAN;
    if (Local.HeaderSize < RPERF_FILE_HEADER_SIZE ||
        Local.HeaderSize > RPERF_MAX_HEADER_BYTES ||
        (Local.HeaderSize & (RPERF_ALIGNMENT - 1)) != 0 ||
        (Local.VersionMinor <= RPERF_VERSION_MINOR &&
         Local.HeaderSize != RPERF_FILE_HEADER_SIZE))
    {
        return RPERF_E_BAD_SIZE;
    }
    if (Local.HeaderSize > BufferSize)
        return RPERF_E_TRUNCATED;
    if (Local.ChecksumAlgorithm != RPERF_CHECKSUM_CRC32_IEEE)
        return RPERF_E_UNSUPPORTED;
    if (Local.CompressionAlgorithm != RPERF_COMPRESSION_NONE)
        return RPERF_E_UNSUPPORTED;
    if (Local.FirstChunkOffset < Local.HeaderSize ||
        (Local.FirstChunkOffset & (RPERF_ALIGNMENT - 1)) != 0)
    {
        return RPERF_E_BAD_SIZE;
    }
    if (Local.Flags & RPERF_FILE_FLAG_FINALIZED)
    {
        uint64_t MinimumFooterSize = RPERF_CHUNK_HEADER_SIZE +
                                     sizeof(RPERF_FOOTER_RECORD_V1) +
                                     RPERF_CHUNK_TRAILER_SIZE;

        if (Local.ChunkCount == 0 ||
            Local.FooterChunkOffset < Local.FirstChunkOffset ||
            (Local.FooterChunkOffset & (RPERF_ALIGNMENT - 1)) != 0 ||
            Local.FileSize < Local.FooterChunkOffset ||
            Local.FileSize - Local.FooterChunkOffset < MinimumFooterSize)
        {
            return RPERF_E_BAD_SIZE;
        }
        if ((Local.IndexChunkOffset == 0) !=
            ((Local.Flags & RPERF_FILE_FLAG_HAS_INDEX) == 0))
        {
            return RPERF_E_CORRUPT;
        }
        if (Local.IndexChunkOffset != 0 &&
            (Local.IndexChunkOffset < Local.FirstChunkOffset ||
             Local.IndexChunkOffset >= Local.FooterChunkOffset ||
             (Local.IndexChunkOffset & (RPERF_ALIGNMENT - 1)) != 0))
        {
            return RPERF_E_BAD_SIZE;
        }
    }
    else if (Local.VersionMinor <= RPERF_VERSION_MINOR &&
             (Local.FileSize != 0 || Local.ChunkCount != 0 ||
              Local.IndexChunkOffset != 0 ||
              Local.FooterChunkOffset != 0 ||
              (Local.Flags & RPERF_FILE_FLAG_HAS_INDEX)))
    {
        return RPERF_E_CORRUPT;
    }
    if ((ReaderFlags & RPERF_READER_FLAG_REQUIRE_FINALIZED) &&
        !(Local.Flags & (RPERF_FILE_FLAG_FINALIZED |
                         RPERF_FILE_FLAG_STREAMING)))
    {
        return RPERF_E_TRUNCATED;
    }
    for (Index = 0;
         Local.VersionMinor <= RPERF_VERSION_MINOR && Index < 5;
         Index++)
    {
        if (Local.Reserved[Index] != 0)
            return RPERF_E_CORRUPT;
    }

    ExpectedCrc = RperfHeaderCrc(Bytes, Local.HeaderSize, 104);
    if (ExpectedCrc != Local.HeaderCrc32)
        return RPERF_E_CHECKSUM;

    if (Header != NULL)
        *Header = Local;
    return RPERF_OK;
}

static void
RperfDecodeChunkHeader(const uint8_t *Bytes, RPERF_CHUNK_HEADER_V1 *Header)
{
    memset(Header, 0, sizeof(*Header));
    Header->Magic = RperfLoadLe32(Bytes);
    Header->Type = RperfLoadLe16(Bytes + 4);
    Header->Version = RperfLoadLe16(Bytes + 6);
    Header->HeaderSize = RperfLoadLe32(Bytes + 8);
    Header->Flags = RperfLoadLe32(Bytes + 12);
    Header->CompressionAlgorithm = RperfLoadLe32(Bytes + 16);
    Header->ChecksumAlgorithm = RperfLoadLe32(Bytes + 20);
    Header->Sequence = RperfLoadLe64(Bytes + 24);
    Header->StoredSize = RperfLoadLe64(Bytes + 32);
    Header->UncompressedSize = RperfLoadLe64(Bytes + 40);
    Header->RecordCount = RperfLoadLe64(Bytes + 48);
    Header->FirstTimestamp = RperfLoadLe64(Bytes + 56);
    Header->LastTimestamp = RperfLoadLe64(Bytes + 64);
    Header->HeaderCrc32 = RperfLoadLe32(Bytes + 72);
    Header->Reserved = RperfLoadLe32(Bytes + 76);
}

static void
RperfEncodeChunkHeader(RPERF_CHUNK_HEADER_V1 *Header,
                       uint8_t Bytes[RPERF_CHUNK_HEADER_SIZE])
{
    memset(Bytes, 0, RPERF_CHUNK_HEADER_SIZE);
    RperfStoreLe32(Bytes, Header->Magic);
    RperfStoreLe16(Bytes + 4, Header->Type);
    RperfStoreLe16(Bytes + 6, Header->Version);
    RperfStoreLe32(Bytes + 8, Header->HeaderSize);
    RperfStoreLe32(Bytes + 12, Header->Flags);
    RperfStoreLe32(Bytes + 16, Header->CompressionAlgorithm);
    RperfStoreLe32(Bytes + 20, Header->ChecksumAlgorithm);
    RperfStoreLe64(Bytes + 24, Header->Sequence);
    RperfStoreLe64(Bytes + 32, Header->StoredSize);
    RperfStoreLe64(Bytes + 40, Header->UncompressedSize);
    RperfStoreLe64(Bytes + 48, Header->RecordCount);
    RperfStoreLe64(Bytes + 56, Header->FirstTimestamp);
    RperfStoreLe64(Bytes + 64, Header->LastTimestamp);
    Header->HeaderCrc32 = RperfHeaderCrc(Bytes, RPERF_CHUNK_HEADER_SIZE, 72);
    RperfStoreLe32(Bytes + 72, Header->HeaderCrc32);
}

static void
RperfEncodeChunkTrailer(const RPERF_CHUNK_TRAILER_V1 *Trailer,
                        uint8_t Bytes[RPERF_CHUNK_TRAILER_SIZE])
{
    memset(Bytes, 0, RPERF_CHUNK_TRAILER_SIZE);
    RperfStoreLe32(Bytes, Trailer->Magic);
    RperfStoreLe32(Bytes + 4, Trailer->Size);
    RperfStoreLe64(Bytes + 8, Trailer->Sequence);
    RperfStoreLe32(Bytes + 16, Trailer->PayloadCrc32);
    RperfStoreLe32(Bytes + 20, Trailer->HeaderCrc32);
    RperfStoreLe64(Bytes + 24, Trailer->StoredSize);
}

static void
RperfDecodeChunkTrailer(const uint8_t *Bytes,
                        RPERF_CHUNK_TRAILER_V1 *Trailer)
{
    Trailer->Magic = RperfLoadLe32(Bytes);
    Trailer->Size = RperfLoadLe32(Bytes + 4);
    Trailer->Sequence = RperfLoadLe64(Bytes + 8);
    Trailer->PayloadCrc32 = RperfLoadLe32(Bytes + 16);
    Trailer->HeaderCrc32 = RperfLoadLe32(Bytes + 20);
    Trailer->StoredSize = RperfLoadLe64(Bytes + 24);
}

static RPERF_STATUS
RperfRecordFixedSize(const uint8_t *Bytes,
                     uint32_t RecordSize,
                     uint32_t HeaderSize,
                     uint32_t ExpectedSize)
{
    (void)Bytes;
    if (HeaderSize != ExpectedSize || RecordSize < HeaderSize)
        return RPERF_E_BAD_SIZE;
    return RPERF_OK;
}

static RPERF_STATUS
RperfRecordExactSize(const uint8_t *Bytes,
                     uint32_t RecordSize,
                     uint32_t HeaderSize,
                     uint32_t ExpectedSize)
{
    RPERF_STATUS Status;

    Status = RperfRecordFixedSize(Bytes, RecordSize, HeaderSize, ExpectedSize);
    if (Status != RPERF_OK)
        return Status;
    return RecordSize == ExpectedSize ? RPERF_OK : RPERF_E_BAD_SIZE;
}

RPERF_STATUS
RperfValidateRecord(const void *Record,
                    uint32_t AvailableBytes,
                    uint32_t MaximumRecordBytes)
{
    const uint8_t *Bytes = (const uint8_t *)Record;
    uint32_t Size, HeaderSize;
    uint16_t Type, Version;
    RPERF_STATUS Status;

    if (Record == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (AvailableBytes < RPERF_RECORD_HEADER_SIZE)
        return RPERF_E_TRUNCATED;
    if (MaximumRecordBytes == 0)
        MaximumRecordBytes = RPERF_DEFAULT_MAX_RECORD_BYTES;
    if (MaximumRecordBytes > RPERF_HARD_MAX_RECORD_BYTES)
        return RPERF_E_LIMIT;

    Size = RperfLoadLe32(Bytes);
    Type = RperfLoadLe16(Bytes + 4);
    Version = RperfLoadLe16(Bytes + 6);
    HeaderSize = RperfLoadLe32(Bytes + 12);
    if (Size < RPERF_RECORD_HEADER_SIZE || Size > AvailableBytes ||
        Size > MaximumRecordBytes ||
        (Size & (RPERF_ALIGNMENT - 1)) != 0 ||
        HeaderSize < RPERF_RECORD_HEADER_SIZE || HeaderSize > Size ||
        (HeaderSize & (RPERF_ALIGNMENT - 1)) != 0)
    {
        return RPERF_E_BAD_SIZE;
    }

    if (Type == 0 || Version == 0)
        return RPERF_E_BAD_VERSION;
    if (Version != 1)
        return RPERF_OK;

    switch (Type)
    {
        case RPERF_RECORD_SESSION:
            Status = RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_SESSION_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            return (RperfLoadLe64(Bytes + offsetof(RPERF_SESSION_RECORD_V1, Reserved)) == 0 &&
                    RperfLoadLe64(Bytes + offsetof(RPERF_SESSION_RECORD_V1, Reserved) + sizeof(uint64_t)) == 0) ?
                   RPERF_OK : RPERF_E_CORRUPT;

        case RPERF_RECORD_SOURCE:
            Status = RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_SOURCE_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            return (RperfLoadLe64(Bytes + offsetof(RPERF_SOURCE_RECORD_V1, Reserved)) == 0 &&
                    RperfLoadLe64(Bytes + offsetof(RPERF_SOURCE_RECORD_V1, Reserved) + sizeof(uint64_t)) == 0) ?
                   RPERF_OK : RPERF_E_CORRUPT;

        case RPERF_RECORD_PROCESS:
            return RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_PROCESS_RECORD_V1));
        case RPERF_RECORD_THREAD:
            return RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_THREAD_RECORD_V1));
        case RPERF_RECORD_SCHEDULER:
            return RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_SCHEDULER_RECORD_V1));
        case RPERF_RECORD_COUNTER:
            Status = RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_COUNTER_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            return RperfLoadLe32(Bytes + offsetof(RPERF_COUNTER_RECORD_V1, Reserved)) == 0 ?
                   RPERF_OK : RPERF_E_CORRUPT;

        case RPERF_RECORD_LOSS:
            return RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_LOSS_RECORD_V1));
        case RPERF_RECORD_CLOCK_SYNC:
            Status = RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_CLOCK_SYNC_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            return RperfLoadLe32(Bytes + offsetof(RPERF_CLOCK_SYNC_RECORD_V1, Reserved)) == 0 ?
                   RPERF_OK : RPERF_E_CORRUPT;

        case RPERF_RECORD_SYMBOL:
            return RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_SYMBOL_RECORD_V1));
        case RPERF_RECORD_FOOTER:
            Status = RperfRecordExactSize(Bytes, Size, HeaderSize, sizeof(RPERF_FOOTER_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            return RperfLoadLe64(Bytes + offsetof(RPERF_FOOTER_RECORD_V1, Reserved)) == 0 ?
                   RPERF_OK : RPERF_E_CORRUPT;

        case RPERF_RECORD_IMAGE:
        {
            uint32_t Offset, ByteCount;

            Status = RperfRecordFixedSize(Bytes, Size, HeaderSize, sizeof(RPERF_IMAGE_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            Offset = RperfLoadLe32(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, BuildIdOffset));
            ByteCount = RperfLoadLe32(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, BuildIdBytes));
            return RperfPayloadRangeValid(Bytes, Size, HeaderSize, Offset, ByteCount) ?
                   RPERF_OK : RPERF_E_BAD_SIZE;
        }

        case RPERF_RECORD_SAMPLE:
        {
            uint32_t Offset, Count, ByteCount;
            uint32_t UserDepth, KernelDepth;
            uint16_t Encoding;

            Status = RperfRecordFixedSize(Bytes, Size, HeaderSize, sizeof(RPERF_SAMPLE_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            Offset = RperfLoadLe32(Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, CallchainOffset));
            Count = RperfLoadLe32(Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, CallchainCount));
            UserDepth = RperfLoadLe16(Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, UserDepth));
            KernelDepth = RperfLoadLe16(Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, KernelDepth));
            Encoding = RperfLoadLe16(Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, CallchainEncoding));
            if (Encoding != 0 && Encoding != RPERF_CALLCHAIN_ADDRESS64)
                return RPERF_E_UNSUPPORTED;
            if (Count != 0 && Encoding == 0)
                return RPERF_E_BAD_SIZE;
            if (Count > RPERF_MAX_CALLCHAIN_DEPTH ||
                Count != UserDepth + KernelDepth ||
                !RperfMultiplyU32(Count, sizeof(uint64_t), &ByteCount) ||
                (Offset != 0 && (Offset & (sizeof(uint64_t) - 1)) != 0) ||
                !RperfPayloadRangeValid(Bytes, Size, HeaderSize, Offset, ByteCount))
            {
                return RPERF_E_BAD_SIZE;
            }
            return RPERF_OK;
        }

        case RPERF_RECORD_STRING:
        {
            uint32_t Offset, ByteCount;
            uint16_t Encoding;

            Status = RperfRecordFixedSize(Bytes, Size, HeaderSize, sizeof(RPERF_STRING_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            Offset = RperfLoadLe32(Bytes + offsetof(RPERF_STRING_RECORD_V1, DataOffset));
            ByteCount = RperfLoadLe32(Bytes + offsetof(RPERF_STRING_RECORD_V1, DataBytes));
            Encoding = RperfLoadLe16(Bytes + offsetof(RPERF_STRING_RECORD_V1, Encoding));
            if (Encoding != RPERF_STRING_UTF8)
                return RPERF_E_UNSUPPORTED;
            if (ByteCount > RPERF_MAX_STRING_BYTES ||
                !RperfPayloadRangeValid(Bytes, Size, HeaderSize, Offset, ByteCount))
            {
                return RPERF_E_BAD_SIZE;
            }
            return RPERF_OK;
        }

        case RPERF_RECORD_MODULE:
        {
            uint32_t Offset, ByteCount;

            Status = RperfRecordFixedSize(Bytes, Size, HeaderSize, sizeof(RPERF_MODULE_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            if (RperfLoadLe64(Bytes + offsetof(RPERF_MODULE_RECORD_V1, Reserved)) != 0)
            {
                return RPERF_E_CORRUPT;
            }
            Offset = RperfLoadLe32(Bytes + offsetof(RPERF_MODULE_RECORD_V1, DebugIdOffset));
            ByteCount = RperfLoadLe32(Bytes + offsetof(RPERF_MODULE_RECORD_V1, DebugIdBytes));
            return RperfPayloadRangeValid(Bytes, Size, HeaderSize, Offset, ByteCount) ?
                   RPERF_OK : RPERF_E_BAD_SIZE;
        }

        case RPERF_RECORD_INDEX:
        {
            uint32_t Count, EntrySize, Offset, ByteCount;

            Status = RperfRecordFixedSize(Bytes, Size, HeaderSize, sizeof(RPERF_INDEX_RECORD_V1));
            if (Status != RPERF_OK)
                return Status;
            if (RperfLoadLe32(Bytes + offsetof(RPERF_INDEX_RECORD_V1, Reserved)) != 0)
            {
                return RPERF_E_CORRUPT;
            }
            Count = RperfLoadLe32(Bytes + offsetof(RPERF_INDEX_RECORD_V1, EntryCount));
            EntrySize = RperfLoadLe32(Bytes + offsetof(RPERF_INDEX_RECORD_V1, EntrySize));
            Offset = RperfLoadLe32(Bytes + offsetof(RPERF_INDEX_RECORD_V1, EntriesOffset));
            if (EntrySize < sizeof(RPERF_INDEX_ENTRY_V1) ||
                (EntrySize & (RPERF_ALIGNMENT - 1)) != 0 ||
                !RperfMultiplyU32(Count, EntrySize, &ByteCount) ||
                !RperfPayloadRangeValid(Bytes, Size, HeaderSize, Offset, ByteCount))
            {
                return RPERF_E_BAD_SIZE;
            }
            return RPERF_OK;
        }

        default:
            return RPERF_OK;
    }
}

static RPERF_STATUS
RperfWriterBeginChunkInternal(RPERF_WRITER *Writer,
                              const RPERF_CHUNK_DESCRIPTOR *Descriptor,
                              int AllowFooter)
{
    uint8_t Bytes[RPERF_CHUNK_HEADER_SIZE];
    uint64_t PayloadEnd;
    RPERF_STATUS Status;

    if (Writer == NULL || Descriptor == NULL ||
        Descriptor->Size < sizeof(*Descriptor))
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    if (Writer->WriteAt == NULL || Writer->Status != RPERF_OK ||
        Writer->Active || (Writer->Flags & RPERF_WRITER_FINALIZED))
    {
        return RPERF_E_STATE;
    }
    if (Descriptor->Type == 0 || Descriptor->Version == 0 ||
        (Descriptor->Type == RPERF_CHUNK_FOOTER && !AllowFooter))
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    if (Descriptor->StoredSize > Writer->MaximumChunkBytes ||
        Descriptor->UncompressedSize > Writer->MaximumChunkBytes)
    {
        return RPERF_E_LIMIT;
    }
    if (Descriptor->CompressionAlgorithm != RPERF_COMPRESSION_NONE)
        return RPERF_E_UNSUPPORTED;
    if (Descriptor->UncompressedSize != Descriptor->StoredSize)
        return RPERF_E_BAD_SIZE;
    if (Descriptor->RecordCount != 0 &&
        Descriptor->FirstTimestamp > Descriptor->LastTimestamp)
    {
        return RPERF_E_BAD_SIZE;
    }
    if (Writer->ChunkCount == UINT64_MAX)
        return RPERF_E_OVERFLOW;
    if (!RperfAddU64(Writer->Offset, RPERF_CHUNK_HEADER_SIZE, &PayloadEnd) ||
        !RperfAddU64(PayloadEnd, Descriptor->StoredSize, &PayloadEnd))
    {
        return RPERF_E_OVERFLOW;
    }
    PayloadEnd = RperfAlignUpU64(PayloadEnd);
    if (PayloadEnd == 0 ||
        !RperfAddU64(PayloadEnd, RPERF_CHUNK_TRAILER_SIZE, &PayloadEnd))
    {
        return RPERF_E_OVERFLOW;
    }

    memset(&Writer->ActiveChunk, 0, sizeof(Writer->ActiveChunk));
    Writer->ActiveChunk.Magic = RPERF_CHUNK_MAGIC;
    Writer->ActiveChunk.Type = Descriptor->Type;
    Writer->ActiveChunk.Version = Descriptor->Version;
    Writer->ActiveChunk.HeaderSize = RPERF_CHUNK_HEADER_SIZE;
    Writer->ActiveChunk.Flags = Descriptor->Flags;
    Writer->ActiveChunk.CompressionAlgorithm =
        Descriptor->CompressionAlgorithm;
    Writer->ActiveChunk.ChecksumAlgorithm = RPERF_CHECKSUM_CRC32_IEEE;
    Writer->ActiveChunk.Sequence = Writer->ChunkCount + 1;
    Writer->ActiveChunk.StoredSize = Descriptor->StoredSize;
    Writer->ActiveChunk.UncompressedSize = Descriptor->UncompressedSize;
    Writer->ActiveChunk.RecordCount = Descriptor->RecordCount;
    Writer->ActiveChunk.FirstTimestamp = Descriptor->FirstTimestamp;
    Writer->ActiveChunk.LastTimestamp = Descriptor->LastTimestamp;
    RperfEncodeChunkHeader(&Writer->ActiveChunk, Bytes);

    Writer->ActiveChunkOffset = Writer->Offset;
    Status = RperfWriteExact(Writer->WriteAt, Writer->Context, Writer->Offset, Bytes, sizeof(Bytes));
    if (Status != RPERF_OK)
    {
        Writer->Status = Status;
        return Status;
    }

    Writer->Offset += sizeof(Bytes);
    Writer->ActivePayloadRemaining = Descriptor->StoredSize;
    Writer->ActivePayloadCrc32 = 0;
    Writer->Active = 1;
    return RPERF_OK;
}

RPERF_STATUS
RperfWriterInitialize(RPERF_WRITER *Writer,
                      RPERF_WRITE_AT_CALLBACK WriteAt,
                      void *Context,
                      const RPERF_WRITER_OPTIONS *Options)
{
    uint8_t Bytes[RPERF_FILE_HEADER_SIZE];
    uint32_t MaximumChunkBytes;
    RPERF_STATUS Status;

    if (Writer == NULL || WriteAt == NULL || Options == NULL ||
        Options->Size < sizeof(*Options))
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    if (Options->Flags & ~RPERF_WRITER_FLAG_STREAMING)
        return RPERF_E_INVALID_ARGUMENT;
    if (Options->FileFlags & ~(RPERF_FILE_FLAG_RECOVERED |
                               RPERF_FILE_FLAG_REDACTED))
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    MaximumChunkBytes = Options->MaximumChunkBytes != 0 ?
                        Options->MaximumChunkBytes :
                        RPERF_DEFAULT_MAX_CHUNK_BYTES;
    if (MaximumChunkBytes > RPERF_HARD_MAX_CHUNK_BYTES)
        return RPERF_E_LIMIT;

    memset(Writer, 0, sizeof(*Writer));
    Writer->WriteAt = WriteAt;
    Writer->Context = Context;
    Writer->MaximumChunkBytes = MaximumChunkBytes;
    Writer->Flags = Options->Flags;
    Writer->Status = RPERF_OK;

    memcpy(Writer->Header.Magic, RperfFileMagic, sizeof(RperfFileMagic));
    Writer->Header.VersionMajor = RPERF_VERSION_MAJOR;
    Writer->Header.VersionMinor = RPERF_VERSION_MINOR;
    Writer->Header.EndianMarker = RPERF_ENDIAN_MARKER;
    Writer->Header.HeaderSize = RPERF_FILE_HEADER_SIZE;
    Writer->Header.Flags = Options->FileFlags;
    if (Options->Flags & RPERF_WRITER_FLAG_STREAMING)
        Writer->Header.Flags |= RPERF_FILE_FLAG_STREAMING;
    Writer->Header.ChecksumAlgorithm = RPERF_CHECKSUM_CRC32_IEEE;
    Writer->Header.CompressionAlgorithm = RPERF_COMPRESSION_NONE;
    memcpy(Writer->Header.SessionId, Options->SessionId, sizeof(Writer->Header.SessionId));
    Writer->Header.CreatedSystemTime100ns =
        Options->CreatedSystemTime100ns;
    Writer->Header.TimestampFrequency = Options->TimestampFrequency;
    Writer->Header.FirstChunkOffset = RPERF_FILE_HEADER_SIZE;

    RperfEncodeFileHeader(&Writer->Header, Bytes);
    Status = RperfWriteExact(WriteAt, Context, 0, Bytes, sizeof(Bytes));
    if (Status != RPERF_OK)
    {
        Writer->Status = Status;
        return Status;
    }
    Writer->Offset = RPERF_FILE_HEADER_SIZE;
    return RPERF_OK;
}

RPERF_STATUS
RperfWriterBeginChunk(RPERF_WRITER *Writer,
                      const RPERF_CHUNK_DESCRIPTOR *Descriptor)
{
    return RperfWriterBeginChunkInternal(Writer, Descriptor, 0);
}

RPERF_STATUS
RperfWriterWriteChunkData(RPERF_WRITER *Writer,
                          const void *Buffer,
                          uint32_t BufferSize)
{
    RPERF_STATUS Status;

    if (Writer == NULL || (Buffer == NULL && BufferSize != 0))
        return RPERF_E_INVALID_ARGUMENT;
    if (!Writer->Active || Writer->Status != RPERF_OK)
        return RPERF_E_STATE;
    if (BufferSize > Writer->ActivePayloadRemaining)
        return RPERF_E_BAD_SIZE;
    if (BufferSize == 0)
        return RPERF_OK;

    Status = RperfWriteExact(Writer->WriteAt, Writer->Context, Writer->Offset, Buffer, BufferSize);
    if (Status != RPERF_OK)
    {
        Writer->Status = Status;
        return Status;
    }
    Writer->ActivePayloadCrc32 = RperfCrc32(Writer->ActivePayloadCrc32, Buffer, BufferSize);
    Writer->Offset += BufferSize;
    Writer->ActivePayloadRemaining -= BufferSize;
    return RPERF_OK;
}

RPERF_STATUS
RperfWriterEndChunk(RPERF_WRITER *Writer)
{
    static const uint8_t Padding[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    RPERF_CHUNK_TRAILER_V1 Trailer;
    uint8_t TrailerBytes[RPERF_CHUNK_TRAILER_SIZE];
    uint32_t PaddingBytes;
    uint64_t AlignedOffset, ChunkEnd;
    RPERF_STATUS Status;

    if (Writer == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (!Writer->Active || Writer->Status != RPERF_OK)
        return RPERF_E_STATE;
    if (Writer->ActivePayloadRemaining != 0)
        return RPERF_E_STATE;

    AlignedOffset = RperfAlignUpU64(Writer->Offset);
    if (AlignedOffset == 0 ||
        !RperfAddU64(AlignedOffset, RPERF_CHUNK_TRAILER_SIZE, &ChunkEnd))
    {
        return RPERF_E_OVERFLOW;
    }
    if (Writer->TotalRecordCount >
        UINT64_MAX - Writer->ActiveChunk.RecordCount)
    {
        return RPERF_E_OVERFLOW;
    }
    PaddingBytes = (uint32_t)(AlignedOffset - Writer->Offset);
    if (PaddingBytes != 0)
    {
        Status = RperfWriteExact(Writer->WriteAt, Writer->Context, Writer->Offset, Padding, PaddingBytes);
        if (Status != RPERF_OK)
        {
            Writer->Status = Status;
            return Status;
        }
        Writer->Offset = AlignedOffset;
    }

    memset(&Trailer, 0, sizeof(Trailer));
    Trailer.Magic = RPERF_TRAILER_MAGIC;
    Trailer.Size = RPERF_CHUNK_TRAILER_SIZE;
    Trailer.Sequence = Writer->ActiveChunk.Sequence;
    Trailer.PayloadCrc32 = Writer->ActivePayloadCrc32;
    Trailer.HeaderCrc32 = Writer->ActiveChunk.HeaderCrc32;
    Trailer.StoredSize = Writer->ActiveChunk.StoredSize;
    RperfEncodeChunkTrailer(&Trailer, TrailerBytes);
    Status = RperfWriteExact(Writer->WriteAt, Writer->Context, Writer->Offset, TrailerBytes, sizeof(TrailerBytes));
    if (Status != RPERF_OK)
    {
        Writer->Status = Status;
        return Status;
    }
    Writer->Offset = ChunkEnd;
    Writer->LastChunkOffset = Writer->ActiveChunkOffset;
    Writer->ChunkCount++;
    Writer->TotalRecordCount += Writer->ActiveChunk.RecordCount;
    if (Writer->ActiveChunk.Type == RPERF_CHUNK_INDEX)
        Writer->IndexChunkOffset = Writer->ActiveChunkOffset;
    Writer->Active = 0;
    return RPERF_OK;
}

RPERF_STATUS
RperfWriterWriteChunk(RPERF_WRITER *Writer,
                      const RPERF_CHUNK_DESCRIPTOR *Descriptor,
                      const void *Payload)
{
    RPERF_STATUS Status;

    if (Descriptor == NULL ||
        (Payload == NULL && Descriptor->StoredSize != 0))
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    if (Descriptor->StoredSize > UINT32_MAX)
        return RPERF_E_LIMIT;
    Status = RperfWriterBeginChunk(Writer, Descriptor);
    if (Status != RPERF_OK)
        return Status;
    Status = RperfWriterWriteChunkData(Writer, Payload, (uint32_t)Descriptor->StoredSize);
    if (Status != RPERF_OK)
        return Status;
    return RperfWriterEndChunk(Writer);
}

RPERF_STATUS
RperfInitializeRecord(void *Record,
                      uint32_t RecordSize,
                      uint16_t Type,
                      uint16_t Version,
                      uint32_t Flags,
                      uint32_t HeaderSize,
                      uint64_t Sequence,
                      uint64_t Timestamp)
{
    uint8_t *Bytes = (uint8_t *)Record;

    if (Record == NULL || Type == 0 || Version == 0 ||
        RecordSize < RPERF_RECORD_HEADER_SIZE ||
        RecordSize > RPERF_HARD_MAX_RECORD_BYTES ||
        HeaderSize < RPERF_RECORD_HEADER_SIZE || HeaderSize > RecordSize ||
        (RecordSize & (RPERF_ALIGNMENT - 1)) != 0 ||
        (HeaderSize & (RPERF_ALIGNMENT - 1)) != 0)
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    memset(Bytes, 0, RecordSize);
    RperfStoreLe32(Bytes, RecordSize);
    RperfStoreLe16(Bytes + 4, Type);
    RperfStoreLe16(Bytes + 6, Version);
    RperfStoreLe32(Bytes + 8, Flags);
    RperfStoreLe32(Bytes + 12, HeaderSize);
    RperfStoreLe64(Bytes + 16, Sequence);
    RperfStoreLe64(Bytes + 24, Timestamp);
    return RPERF_OK;
}

RPERF_STATUS
RperfWriterFinalize(RPERF_WRITER *Writer,
                    int32_t FinalStatus,
                    uint32_t FooterFlags,
                    uint64_t TotalLostRecords)
{
    uint8_t Footer[sizeof(RPERF_FOOTER_RECORD_V1)];
    uint8_t HeaderBytes[RPERF_FILE_HEADER_SIZE];
    RPERF_CHUNK_DESCRIPTOR Descriptor;
    uint64_t FooterOffset, FinalFileSize, FinalChunkCount;
    uint64_t PreviousChunkOffset;
    RPERF_STATUS Status;

    if (Writer == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (Writer->Status != RPERF_OK || Writer->Active ||
        (Writer->Flags & RPERF_WRITER_FINALIZED))
    {
        return RPERF_E_STATE;
    }

    FooterOffset = Writer->Offset;
    PreviousChunkOffset = Writer->LastChunkOffset;
    if (Writer->ChunkCount == UINT64_MAX ||
        Writer->TotalRecordCount == UINT64_MAX)
    {
        return RPERF_E_OVERFLOW;
    }
    if (!RperfAddU64(FooterOffset, RPERF_CHUNK_HEADER_SIZE + sizeof(Footer) + RPERF_CHUNK_TRAILER_SIZE, &FinalFileSize))
    {
        return RPERF_E_OVERFLOW;
    }
    FinalChunkCount = Writer->ChunkCount + 1;

    memset(Footer, 0, sizeof(Footer));
    Status = RperfInitializeRecord(Footer, sizeof(Footer), RPERF_RECORD_FOOTER, 1, 0, sizeof(Footer), Writer->TotalRecordCount + 1, 0);
    if (Status != RPERF_OK)
        return Status;
    RperfStoreLe64(Footer + offsetof(RPERF_FOOTER_RECORD_V1, FileSize), FinalFileSize);
    RperfStoreLe64(Footer + offsetof(RPERF_FOOTER_RECORD_V1, ChunkCount), FinalChunkCount);
    RperfStoreLe64(Footer + offsetof(RPERF_FOOTER_RECORD_V1, LastChunkOffset), PreviousChunkOffset);
    RperfStoreLe64(Footer + offsetof(RPERF_FOOTER_RECORD_V1, IndexChunkOffset), Writer->IndexChunkOffset);
    RperfStoreLe64(Footer + offsetof(RPERF_FOOTER_RECORD_V1, TotalRecordCount), Writer->TotalRecordCount + 1);
    RperfStoreLe64(Footer + offsetof(RPERF_FOOTER_RECORD_V1, TotalLostRecords), TotalLostRecords);
    RperfStoreLe32(Footer + offsetof(RPERF_FOOTER_RECORD_V1, FinalStatus), (uint32_t)FinalStatus);
    RperfStoreLe32(Footer + offsetof(RPERF_FOOTER_RECORD_V1, FooterFlags), FooterFlags);

    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.Size = sizeof(Descriptor);
    Descriptor.Type = RPERF_CHUNK_FOOTER;
    Descriptor.Version = 1;
    Descriptor.CompressionAlgorithm = RPERF_COMPRESSION_NONE;
    Descriptor.StoredSize = sizeof(Footer);
    Descriptor.UncompressedSize = sizeof(Footer);
    Descriptor.RecordCount = 1;
    Status = RperfWriterBeginChunkInternal(Writer, &Descriptor, 1);
    if (Status == RPERF_OK)
        Status = RperfWriterWriteChunkData(Writer, Footer, sizeof(Footer));
    if (Status == RPERF_OK)
        Status = RperfWriterEndChunk(Writer);
    if (Status != RPERF_OK)
        return Status;
    if (Writer->Offset != FinalFileSize ||
        Writer->ChunkCount != FinalChunkCount)
    {
        Writer->Status = RPERF_E_STATE;
        return Writer->Status;
    }

    Writer->Header.FileSize = Writer->Offset;
    Writer->Header.ChunkCount = Writer->ChunkCount;
    Writer->Header.IndexChunkOffset = Writer->IndexChunkOffset;
    Writer->Header.FooterChunkOffset = FooterOffset;
    Writer->Header.Flags |= RPERF_FILE_FLAG_FINALIZED;
    if (Writer->IndexChunkOffset != 0)
        Writer->Header.Flags |= RPERF_FILE_FLAG_HAS_INDEX;
    Writer->Flags |= RPERF_WRITER_FINALIZED;

    if (!(Writer->Flags & RPERF_WRITER_FLAG_STREAMING))
    {
        RperfEncodeFileHeader(&Writer->Header, HeaderBytes);
        Status = RperfWriteExact(Writer->WriteAt, Writer->Context, 0, HeaderBytes, sizeof(HeaderBytes));
        if (Status != RPERF_OK)
        {
            Writer->Status = Status;
            return Status;
        }
    }
    return RPERF_OK;
}

RPERF_STATUS
RperfReaderInitialize(RPERF_READER *Reader,
                      RPERF_READ_AT_CALLBACK ReadAt,
                      void *Context,
                      const RPERF_READER_OPTIONS *Options)
{
    uint8_t Bytes[RPERF_MAX_HEADER_BYTES];
    uint32_t HeaderSize, HeaderBytes = RPERF_FILE_HEADER_SIZE;
    uint32_t MaximumChunkBytes = RPERF_DEFAULT_MAX_CHUNK_BYTES;
    uint32_t MaximumRecordBytes = RPERF_DEFAULT_MAX_RECORD_BYTES;
    uint32_t Flags = 0;
    uint64_t FileSizeLimit = 0;
    RPERF_STATUS Status;

    if (Reader == NULL || ReadAt == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (Options != NULL)
    {
        if (Options->Size < sizeof(*Options))
            return RPERF_E_INVALID_ARGUMENT;
        Flags = Options->Flags;
        if (Flags & ~(RPERF_READER_FLAG_ALLOW_NEWER_MINOR |
                      RPERF_READER_FLAG_REQUIRE_FINALIZED |
                      RPERF_READER_FLAG_REQUIRE_ZERO_PADDING))
        {
            return RPERF_E_INVALID_ARGUMENT;
        }
        if (Options->MaximumChunkBytes != 0)
            MaximumChunkBytes = Options->MaximumChunkBytes;
        if (Options->MaximumRecordBytes != 0)
            MaximumRecordBytes = Options->MaximumRecordBytes;
        FileSizeLimit = Options->FileSizeLimit;
    }
    if (MaximumChunkBytes > RPERF_HARD_MAX_CHUNK_BYTES ||
        MaximumRecordBytes > RPERF_HARD_MAX_RECORD_BYTES)
    {
        return RPERF_E_LIMIT;
    }

    memset(Reader, 0, sizeof(*Reader));
    Reader->ReadAt = ReadAt;
    Reader->Context = Context;
    Reader->MaximumChunkBytes = MaximumChunkBytes;
    Reader->MaximumRecordBytes = MaximumRecordBytes;
    Reader->Flags = Flags;
    Reader->Status = RPERF_OK;

    Status = RperfReadExact(ReadAt, Context, 0, Bytes, RPERF_FILE_HEADER_SIZE);
    if (Status != RPERF_OK)
    {
        Reader->Status = Status;
        return Status;
    }
    HeaderSize = RperfLoadLe32(Bytes + offsetof(RPERF_FILE_HEADER_V2, HeaderSize));
    if (HeaderSize > RPERF_FILE_HEADER_SIZE &&
        HeaderSize <= RPERF_MAX_HEADER_BYTES &&
        (HeaderSize & (RPERF_ALIGNMENT - 1)) == 0)
    {
        if (FileSizeLimit != 0 && HeaderSize > FileSizeLimit)
        {
            Reader->Status = RPERF_E_TRUNCATED;
            return Reader->Status;
        }
        Status = RperfReadExact(ReadAt, Context, RPERF_FILE_HEADER_SIZE, Bytes + RPERF_FILE_HEADER_SIZE, HeaderSize - RPERF_FILE_HEADER_SIZE);
        if (Status != RPERF_OK)
        {
            Reader->Status = Status;
            return Status;
        }
        HeaderBytes = HeaderSize;
    }
    Status = RperfValidateFileHeader(Bytes, HeaderBytes, Flags, &Reader->Header);
    if (Status != RPERF_OK)
    {
        Reader->Status = Status;
        return Status;
    }

    if (Reader->Header.FileSize != 0)
    {
        if (FileSizeLimit != 0 &&
            Reader->Header.FileSize > FileSizeLimit)
        {
            Reader->Status = RPERF_E_TRUNCATED;
            return Reader->Status;
        }
        FileSizeLimit = Reader->Header.FileSize;
    }
    else if (FileSizeLimit == 0)
        FileSizeLimit = UINT64_MAX;
    if (Reader->Header.FirstChunkOffset > FileSizeLimit)
    {
        Reader->Status = RPERF_E_TRUNCATED;
        return Reader->Status;
    }
    Reader->FileSizeLimit = FileSizeLimit;
    Reader->NextChunkOffset = Reader->Header.FirstChunkOffset;
    Reader->NextChunkSequence = 1;
    return RPERF_OK;
}

static RPERF_STATUS
RperfReaderValidateChunkHeader(RPERF_READER *Reader,
                               uint64_t ChunkOffset,
                               const uint8_t BaseBytes[RPERF_CHUNK_HEADER_SIZE],
                               RPERF_CHUNK_HEADER_V1 *Header)
{
    uint8_t Extension[256];
    uint8_t BaseCopy[RPERF_CHUNK_HEADER_SIZE];
    uint64_t Cursor, End;
    uint32_t Crc, ToRead;
    RPERF_STATUS Status;

    RperfDecodeChunkHeader(BaseBytes, Header);
    if (Header->Magic != RPERF_CHUNK_MAGIC)
        return RPERF_E_BAD_MAGIC;
    if (Header->Type == 0 || Header->Version == 0)
        return RPERF_E_BAD_VERSION;
    if (Header->Sequence == 0)
        return RPERF_E_CORRUPT;
    if (Header->HeaderSize < RPERF_CHUNK_HEADER_SIZE ||
        Header->HeaderSize > RPERF_MAX_HEADER_BYTES ||
        (Header->HeaderSize & (RPERF_ALIGNMENT - 1)) != 0)
    {
        return RPERF_E_BAD_SIZE;
    }
    if (Header->ChecksumAlgorithm != RPERF_CHECKSUM_CRC32_IEEE)
        return RPERF_E_UNSUPPORTED;
    if (Header->StoredSize > Reader->MaximumChunkBytes ||
        Header->UncompressedSize > Reader->MaximumChunkBytes)
    {
        return RPERF_E_LIMIT;
    }
    if (Header->CompressionAlgorithm == RPERF_COMPRESSION_NONE &&
        Header->StoredSize != Header->UncompressedSize)
    {
        return RPERF_E_BAD_SIZE;
    }
    if (Header->RecordCount != 0 &&
        Header->FirstTimestamp > Header->LastTimestamp)
    {
        return RPERF_E_CORRUPT;
    }
    if (Header->Version == 1 && Header->Reserved != 0)
        return RPERF_E_CORRUPT;

    memcpy(BaseCopy, BaseBytes, sizeof(BaseCopy));
    memset(BaseCopy + 72, 0, sizeof(uint32_t));
    Crc = RperfCrc32(0, BaseCopy, sizeof(BaseCopy));
    if (!RperfAddU64(ChunkOffset, RPERF_CHUNK_HEADER_SIZE, &Cursor) ||
        !RperfAddU64(ChunkOffset, Header->HeaderSize, &End))
    {
        return RPERF_E_OVERFLOW;
    }
    if (End > Reader->FileSizeLimit)
    {
        return RPERF_E_TRUNCATED;
    }
    while (Cursor < End)
    {
        uint64_t Remaining = End - Cursor;

        ToRead = Remaining < sizeof(Extension) ?
                 (uint32_t)Remaining : (uint32_t)sizeof(Extension);
        Status = RperfReadExact(Reader->ReadAt, Reader->Context, Cursor, Extension, ToRead);
        if (Status != RPERF_OK)
            return Status;
        Crc = RperfCrc32(Crc, Extension, ToRead);
        Cursor += ToRead;
    }
    if (Crc != Header->HeaderCrc32)
        return RPERF_E_CHECKSUM;
    return RPERF_OK;
}

RPERF_STATUS
RperfReaderNextChunk(RPERF_READER *Reader,
                     RPERF_CHUNK_HEADER_V1 *ChunkHeader)
{
    uint8_t Bytes[RPERF_CHUNK_HEADER_SIZE];
    uint64_t PayloadOffset, PayloadEnd, TrailerOffset, ChunkEnd;
    RPERF_STATUS Status;

    if (Reader == NULL || Reader->ReadAt == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (Reader->Active)
        return RPERF_E_STATE;
    if (Reader->Status < RPERF_OK)
        return Reader->Status;
    if (Reader->NextChunkOffset >= Reader->FileSizeLimit)
    {
        if (!Reader->FooterSeen &&
            ((Reader->Header.Flags & RPERF_FILE_FLAG_FINALIZED) ||
             (Reader->Flags & RPERF_READER_FLAG_REQUIRE_FINALIZED)))
        {
            Reader->Status = RPERF_E_TRUNCATED;
            return Reader->Status;
        }
        return RPERF_S_END_OF_FILE;
    }
    if ((Reader->Header.Flags & RPERF_FILE_FLAG_FINALIZED) &&
        Reader->NextChunkOffset > Reader->Header.FooterChunkOffset)
    {
        Reader->Status = RPERF_E_CORRUPT;
        return Reader->Status;
    }

    Status = RperfReadExact(Reader->ReadAt, Reader->Context, Reader->NextChunkOffset, Bytes, sizeof(Bytes));
    if (Status != RPERF_OK)
    {
        Reader->Status = Status;
        return Status;
    }
    Status = RperfReaderValidateChunkHeader(Reader, Reader->NextChunkOffset, Bytes, &Reader->ActiveChunk);
    if (Status != RPERF_OK)
    {
        Reader->Status = Status;
        return Status;
    }
    if (Reader->NextChunkSequence != 0 &&
        Reader->ActiveChunk.Sequence != Reader->NextChunkSequence)
    {
        Reader->Status = RPERF_E_CORRUPT;
        return Reader->Status;
    }
    if ((Reader->Header.Flags & RPERF_FILE_FLAG_FINALIZED) &&
        (Reader->ActiveChunk.Sequence > Reader->Header.ChunkCount ||
         (Reader->ActiveChunk.Type != RPERF_CHUNK_FOOTER &&
          Reader->ActiveChunk.Sequence == Reader->Header.ChunkCount)))
    {
        Reader->Status = RPERF_E_CORRUPT;
        return Reader->Status;
    }
    if ((Reader->Header.Flags & RPERF_FILE_FLAG_FINALIZED) &&
        ((Reader->NextChunkOffset == Reader->Header.FooterChunkOffset) !=
         (Reader->ActiveChunk.Type == RPERF_CHUNK_FOOTER)))
    {
        Reader->Status = RPERF_E_CORRUPT;
        return Reader->Status;
    }
    if (Reader->Header.IndexChunkOffset != 0 &&
        Reader->NextChunkOffset == Reader->Header.IndexChunkOffset &&
        Reader->ActiveChunk.Type != RPERF_CHUNK_INDEX)
    {
        Reader->Status = RPERF_E_CORRUPT;
        return Reader->Status;
    }

    if (!RperfAddU64(Reader->NextChunkOffset, Reader->ActiveChunk.HeaderSize, &PayloadOffset) ||
        !RperfAddU64(PayloadOffset, Reader->ActiveChunk.StoredSize, &PayloadEnd))
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }
    TrailerOffset = RperfAlignUpU64(PayloadEnd);
    if (TrailerOffset == 0 ||
        !RperfAddU64(TrailerOffset, RPERF_CHUNK_TRAILER_SIZE, &ChunkEnd))
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }
    if (ChunkEnd > Reader->FileSizeLimit)
    {
        Reader->Status = RPERF_E_TRUNCATED;
        return Reader->Status;
    }
    if ((Reader->Header.Flags & RPERF_FILE_FLAG_FINALIZED) &&
        Reader->ActiveChunk.Type != RPERF_CHUNK_FOOTER &&
        ChunkEnd > Reader->Header.FooterChunkOffset)
    {
        Reader->Status = RPERF_E_CORRUPT;
        return Reader->Status;
    }

    Reader->ActiveChunkOffset = Reader->NextChunkOffset;
    Reader->ActivePayloadOffset = PayloadOffset;
    Reader->ActivePayloadRemaining = Reader->ActiveChunk.StoredSize;
    Reader->ActivePayloadCrc32 = 0;
    Reader->Active = 1;
    Reader->Status = RPERF_OK;
    if (ChunkHeader != NULL)
        *ChunkHeader = Reader->ActiveChunk;
    return RPERF_OK;
}

RPERF_STATUS
RperfReaderReadChunkData(RPERF_READER *Reader,
                         void *Buffer,
                         uint32_t BufferSize,
                         uint32_t *BytesRead)
{
    uint32_t ToRead;
    RPERF_STATUS Status;

    if (Reader == NULL || BytesRead == NULL ||
        (Buffer == NULL && BufferSize != 0))
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    *BytesRead = 0;
    if (!Reader->Active || Reader->Status != RPERF_OK)
        return RPERF_E_STATE;
    if (BufferSize == 0 || Reader->ActivePayloadRemaining == 0)
        return RPERF_OK;

    ToRead = Reader->ActivePayloadRemaining < BufferSize ?
             (uint32_t)Reader->ActivePayloadRemaining : BufferSize;
    Status = RperfReadExact(Reader->ReadAt, Reader->Context, Reader->ActivePayloadOffset, Buffer, ToRead);
    if (Status != RPERF_OK)
    {
        Reader->Status = Status;
        return Status;
    }
    Reader->ActivePayloadCrc32 = RperfCrc32(Reader->ActivePayloadCrc32, Buffer, ToRead);
    Reader->ActivePayloadOffset += ToRead;
    Reader->ActivePayloadRemaining -= ToRead;
    *BytesRead = ToRead;
    return RPERF_OK;
}

static RPERF_STATUS
RperfReaderValidateFooterChunk(RPERF_READER *Reader, uint64_t ChunkEnd)
{
    uint8_t FooterBytes[sizeof(RPERF_FOOTER_RECORD_V1)];
    uint64_t PayloadOffset, FileSize, ChunkCount;
    uint64_t LastChunkOffset, IndexChunkOffset, TotalRecordCount;
    RPERF_STATUS Status;

    if (Reader->ActiveChunk.Type != RPERF_CHUNK_FOOTER)
        return RPERF_OK;
    if (Reader->ActiveChunk.Version != 1)
        return RPERF_E_UNSUPPORTED;
    if (Reader->ActiveChunk.CompressionAlgorithm != RPERF_COMPRESSION_NONE)
        return RPERF_E_UNSUPPORTED;
    if (Reader->ActiveChunk.StoredSize != sizeof(FooterBytes) ||
        Reader->ActiveChunk.UncompressedSize != sizeof(FooterBytes) ||
        Reader->ActiveChunk.RecordCount != 1)
    {
        return RPERF_E_BAD_SIZE;
    }
    if (!RperfAddU64(Reader->ActiveChunkOffset, Reader->ActiveChunk.HeaderSize, &PayloadOffset))
    {
        return RPERF_E_OVERFLOW;
    }
    Status = RperfReadExact(Reader->ReadAt, Reader->Context, PayloadOffset, FooterBytes, sizeof(FooterBytes));
    if (Status != RPERF_OK)
        return Status;
    Status = RperfValidateRecord(FooterBytes, sizeof(FooterBytes), Reader->MaximumRecordBytes);
    if (Status != RPERF_OK)
        return Status;
    if (RperfLoadLe32(FooterBytes) != sizeof(FooterBytes) ||
        RperfLoadLe16(FooterBytes + 4) != RPERF_RECORD_FOOTER ||
        RperfLoadLe16(FooterBytes + 6) != 1 ||
        RperfLoadLe32(FooterBytes + 12) != sizeof(FooterBytes))
    {
        return RPERF_E_CORRUPT;
    }

    FileSize = RperfLoadLe64(FooterBytes + offsetof(RPERF_FOOTER_RECORD_V1, FileSize));
    ChunkCount = RperfLoadLe64(FooterBytes + offsetof(RPERF_FOOTER_RECORD_V1, ChunkCount));
    LastChunkOffset = RperfLoadLe64(FooterBytes + offsetof(RPERF_FOOTER_RECORD_V1, LastChunkOffset));
    IndexChunkOffset = RperfLoadLe64(FooterBytes + offsetof(RPERF_FOOTER_RECORD_V1, IndexChunkOffset));
    TotalRecordCount = RperfLoadLe64(FooterBytes + offsetof(RPERF_FOOTER_RECORD_V1, TotalRecordCount));

    if (FileSize != ChunkEnd ||
        ChunkCount == 0 ||
        ChunkCount != Reader->ActiveChunk.Sequence ||
        TotalRecordCount == 0 ||
        RperfLoadLe64(FooterBytes + offsetof(RPERF_RECORD_HEADER_V1, Sequence)) !=
            TotalRecordCount ||
        RperfLoadLe64(FooterBytes + offsetof(RPERF_FOOTER_RECORD_V1, Reserved)) != 0)
    {
        return RPERF_E_CORRUPT;
    }
    if (!Reader->RecoveryUsed &&
        TotalRecordCount != Reader->TotalRecordCount + 1)
    {
        return RPERF_E_CORRUPT;
    }
    if ((ChunkCount == 1 && LastChunkOffset != 0) ||
        (ChunkCount > 1 &&
         (LastChunkOffset < Reader->Header.FirstChunkOffset ||
          LastChunkOffset >= Reader->ActiveChunkOffset ||
          (LastChunkOffset & (RPERF_ALIGNMENT - 1)) != 0)))
    {
        return RPERF_E_CORRUPT;
    }
    if (IndexChunkOffset != 0 &&
        (IndexChunkOffset < Reader->Header.FirstChunkOffset ||
         IndexChunkOffset >= Reader->ActiveChunkOffset ||
         (IndexChunkOffset & (RPERF_ALIGNMENT - 1)) != 0))
    {
        return RPERF_E_CORRUPT;
    }
    if ((Reader->Header.FileSize != 0 &&
         Reader->Header.FileSize != FileSize) ||
        (Reader->Header.ChunkCount != 0 &&
         Reader->Header.ChunkCount != ChunkCount) ||
        (Reader->Header.FooterChunkOffset != 0 &&
         Reader->Header.FooterChunkOffset != Reader->ActiveChunkOffset) ||
        (Reader->Header.IndexChunkOffset != 0 &&
         Reader->Header.IndexChunkOffset != IndexChunkOffset))
    {
        return RPERF_E_CORRUPT;
    }
    if ((Reader->Header.Flags & RPERF_FILE_FLAG_FINALIZED) &&
        (Reader->Header.FooterChunkOffset != Reader->ActiveChunkOffset ||
         Reader->Header.IndexChunkOffset != IndexChunkOffset))
    {
        return RPERF_E_CORRUPT;
    }

    Reader->FileSizeLimit = ChunkEnd;
    Reader->FooterSeen = 1;
    return RPERF_OK;
}

RPERF_STATUS
RperfReaderFinishChunk(RPERF_READER *Reader)
{
    uint8_t Padding[8];
    uint8_t TrailerBytes[RPERF_CHUNK_TRAILER_SIZE];
    RPERF_CHUNK_TRAILER_V1 Trailer;
    uint64_t TrailerOffset, ChunkEnd, TotalRecordCount;
    uint32_t PaddingBytes, Index;
    RPERF_STATUS Status;

    if (Reader == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (!Reader->Active || Reader->Status != RPERF_OK)
        return RPERF_E_STATE;
    if (Reader->ActivePayloadRemaining != 0)
        return RPERF_E_STATE;

    TrailerOffset = RperfAlignUpU64(Reader->ActivePayloadOffset);
    if (TrailerOffset == 0)
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }
    PaddingBytes = (uint32_t)(TrailerOffset - Reader->ActivePayloadOffset);
    if (PaddingBytes != 0)
    {
        Status = RperfReadExact(Reader->ReadAt, Reader->Context, Reader->ActivePayloadOffset, Padding, PaddingBytes);
        if (Status != RPERF_OK)
        {
            Reader->Status = Status;
            return Status;
        }
        if (Reader->Flags & RPERF_READER_FLAG_REQUIRE_ZERO_PADDING)
        {
            for (Index = 0; Index < PaddingBytes; Index++)
            {
                if (Padding[Index] != 0)
                {
                    Reader->Status = RPERF_E_CORRUPT;
                    return Reader->Status;
                }
            }
        }
    }

    Status = RperfReadExact(Reader->ReadAt, Reader->Context, TrailerOffset, TrailerBytes, sizeof(TrailerBytes));
    if (Status != RPERF_OK)
    {
        Reader->Status = Status;
        return Status;
    }
    RperfDecodeChunkTrailer(TrailerBytes, &Trailer);
    if (Trailer.Magic != RPERF_TRAILER_MAGIC ||
        Trailer.Size != RPERF_CHUNK_TRAILER_SIZE ||
        Trailer.Sequence != Reader->ActiveChunk.Sequence ||
        Trailer.HeaderCrc32 != Reader->ActiveChunk.HeaderCrc32 ||
        Trailer.StoredSize != Reader->ActiveChunk.StoredSize)
    {
        Reader->Status = RPERF_E_CORRUPT;
        return Reader->Status;
    }
    if (Trailer.PayloadCrc32 != Reader->ActivePayloadCrc32)
    {
        Reader->Status = RPERF_E_CHECKSUM;
        return Reader->Status;
    }
    if (!RperfAddU64(TrailerOffset, RPERF_CHUNK_TRAILER_SIZE, &ChunkEnd))
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }
    if (!RperfAddU64(Reader->TotalRecordCount, Reader->ActiveChunk.RecordCount, &TotalRecordCount))
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }
    Status = RperfReaderValidateFooterChunk(Reader, ChunkEnd);
    if (Status != RPERF_OK)
    {
        Reader->Status = Status;
        return Status;
    }
    if (Reader->ActiveChunk.Sequence == UINT64_MAX &&
        Reader->ActiveChunk.Type != RPERF_CHUNK_FOOTER)
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }

    Reader->NextChunkOffset = ChunkEnd;
    Reader->NextChunkSequence =
        Reader->ActiveChunk.Type == RPERF_CHUNK_FOOTER ?
        0 : Reader->ActiveChunk.Sequence + 1;
    Reader->TotalRecordCount = TotalRecordCount;
    Reader->Active = 0;
    Reader->Status = RPERF_OK;
    return RPERF_OK;
}

RPERF_STATUS
RperfReaderSkipChunk(RPERF_READER *Reader)
{
    uint8_t Buffer[4096];
    RPERF_STATUS Status;

    if (Reader == NULL)
        return RPERF_E_INVALID_ARGUMENT;
    if (!Reader->Active)
        return RPERF_E_STATE;

    while (Reader->ActivePayloadRemaining != 0)
    {
        uint32_t Read;

        Status = RperfReaderReadChunkData(Reader, Buffer, sizeof(Buffer), &Read);
        if (Status != RPERF_OK)
            return Status;
        if (Read == 0)
            return RPERF_E_TRUNCATED;
    }
    return RperfReaderFinishChunk(Reader);
}

RPERF_STATUS
RperfReaderRecover(RPERF_READER *Reader,
                   uint64_t MaximumScanBytes,
                   uint64_t *BytesSkipped)
{
    uint8_t MagicBytes[4];
    uint64_t OriginalOffset, Start, End, Candidate;

    if (Reader == NULL || Reader->ReadAt == NULL || MaximumScanBytes == 0)
        return RPERF_E_INVALID_ARGUMENT;
    OriginalOffset = Reader->Active ?
                     Reader->ActiveChunkOffset : Reader->NextChunkOffset;
    Reader->Active = 0;
    Reader->Status = RPERF_OK;
    if (!RperfAddU64(OriginalOffset, RPERF_ALIGNMENT, &Start))
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }
    Start = RperfAlignUpU64(Start);
    if (Start == 0)
    {
        Reader->Status = RPERF_E_OVERFLOW;
        return Reader->Status;
    }
    if (!RperfAddU64(Start, MaximumScanBytes, &End))
        End = UINT64_MAX;
    if (End > Reader->FileSizeLimit)
        End = Reader->FileSizeLimit;

    Candidate = Start;
    while (Candidate < End && End - Candidate >= sizeof(MagicBytes))
    {
        RPERF_READER Trial;
        RPERF_STATUS Status;

        Status = RperfReadExact(Reader->ReadAt, Reader->Context, Candidate, MagicBytes, sizeof(MagicBytes));
        if (Status != RPERF_OK)
            break;
        if (RperfLoadLe32(MagicBytes) == RPERF_CHUNK_MAGIC)
        {
            Trial = *Reader;
            Trial.NextChunkOffset = Candidate;
            Trial.NextChunkSequence = 0;
            Trial.Active = 0;
            Trial.RecoveryUsed = 1;
            Trial.Status = RPERF_OK;
            Status = RperfReaderNextChunk(&Trial, NULL);
            if (Status == RPERF_OK)
                Status = RperfReaderSkipChunk(&Trial);
            if (Status == RPERF_OK)
            {
                Reader->NextChunkOffset = Candidate;
                Reader->NextChunkSequence = 0;
                Reader->Active = 0;
                Reader->FooterSeen = 0;
                Reader->RecoveryUsed = 1;
                Reader->Status = RPERF_S_RECOVERED;
                if (BytesSkipped != NULL)
                    *BytesSkipped = Candidate - OriginalOffset;
                return RPERF_S_RECOVERED;
            }
        }

        if (Candidate > UINT64_MAX - RPERF_ALIGNMENT)
            break;
        Candidate += RPERF_ALIGNMENT;
    }

    if (BytesSkipped != NULL)
        *BytesSkipped = 0;
    Reader->Status = RPERF_E_NOT_FOUND;
    return Reader->Status;
}

void
RperfRecordIteratorInitialize(RPERF_RECORD_ITERATOR *Iterator,
                              const void *Data,
                              uint32_t DataBytes,
                              uint32_t MaximumRecordBytes)
{
    if (Iterator == NULL)
        return;
    Iterator->Data = (const uint8_t *)Data;
    Iterator->Bytes = DataBytes;
    Iterator->Offset = 0;
    Iterator->MaximumRecordBytes = MaximumRecordBytes != 0 ?
                                   MaximumRecordBytes :
                                   RPERF_DEFAULT_MAX_RECORD_BYTES;
}

RPERF_STATUS
RperfRecordIteratorNext(RPERF_RECORD_ITERATOR *Iterator,
                        const RPERF_RECORD_HEADER_V1 **Record)
{
    const uint8_t *Bytes;
    uint32_t Size;
    RPERF_STATUS Status;

    if (Iterator == NULL || Record == NULL ||
        (Iterator->Data == NULL && Iterator->Bytes != 0))
    {
        return RPERF_E_INVALID_ARGUMENT;
    }
    *Record = NULL;
    if (Iterator->Offset == Iterator->Bytes)
        return RPERF_S_END_OF_FILE;
    if (Iterator->Offset > Iterator->Bytes ||
        Iterator->Bytes - Iterator->Offset < RPERF_RECORD_HEADER_SIZE)
    {
        return RPERF_E_TRUNCATED;
    }

    Bytes = Iterator->Data + Iterator->Offset;
    Size = RperfLoadLe32(Bytes);
    Status = RperfValidateRecord(Bytes, Iterator->Bytes - Iterator->Offset, Iterator->MaximumRecordBytes);
    if (Status != RPERF_OK)
        return Status;
    *Record = (const RPERF_RECORD_HEADER_V1 *)Bytes;
    Iterator->Offset += Size;
    return RPERF_OK;
}
