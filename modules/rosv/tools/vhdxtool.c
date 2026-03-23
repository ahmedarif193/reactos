/*
 * PROJECT:     ReactOS VHDX Tool (vhdxtool.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     User-mode VHDX creation, conversion, and inspection utility
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Usage:
 *   vhdxtool create --type dynamic --size <N>G [--block-size <N>M] output.vhdx
 *   vhdxtool create --type fixed   --size <N>G [--block-size <N>M] output.vhdx
 *   vhdxtool convert input.img output.vhdx [--type dynamic|fixed]
 *   vhdxtool info input.vhdx
 */

/*
 * Tell vhdx.h to skip the kernel-mode rosv.h include — we provide our own
 * types via windows.h.
 */
#define VHDX_USER_MODE

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Kernel types / macros not provided by user-mode windows.h */
#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif
#ifndef ULONG64
typedef unsigned __int64 ULONG64;
typedef ULONG64 *PULONG64;
#endif
#ifndef BOOLEAN
typedef UCHAR BOOLEAN;
#endif
#ifndef C_ASSERT
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#endif
/* RtlCompareMemory is already declared in windows.h via winnt.h */

/* Stub the ROSV logging macros (kernel-only) */
#define ROSV_ERR(...)   do { } while(0)
#define ROSV_TRACE(...) do { } while(0)

/* VHDX on-disk structures from the shared header */
#include <rosv/vhdx.h>

/* ========================================================================== */
/* CRC-32C (Castagnoli) - inline implementation for user-mode                  */
/* Polynomial 0x1EDC6F41 (reflected: 0x82F63B78)                               */
/* ========================================================================== */

static const ULONG Crc32cTable[256] = {
    0x00000000, 0xF26B8303, 0xE13B70F7, 0x1350F3F4,
    0xC79A971F, 0x35F1141C, 0x26A1E7E8, 0xD4CA64EB,
    0x8AD958CF, 0x78B2DBCC, 0x6BE22838, 0x9989AB3B,
    0x4D43CFD0, 0xBF284CD3, 0xAC78BF27, 0x5E133C24,
    0x105EC76F, 0xE235446C, 0xF165B798, 0x030E349B,
    0xD7C45070, 0x25AFD373, 0x36FF2087, 0xC494A384,
    0x9A879FA0, 0x68EC1CA3, 0x7BBCEF57, 0x89D76C54,
    0x5D1D08BF, 0xAF768BBC, 0xBC267848, 0x4E4DFB4B,
    0x20BD8EDE, 0xD2D60DDD, 0xC186FE29, 0x33ED7D2A,
    0xE72719C1, 0x154C9AC2, 0x061C6936, 0xF477EA35,
    0xAA64D611, 0x580F5512, 0x4B5FA6E6, 0xB93425E5,
    0x6DFE410E, 0x9F95C20D, 0x8CC531F9, 0x7EAEB2FA,
    0x30E349B1, 0xC288CAB2, 0xD1D83946, 0x23B3BA45,
    0xF779DEAE, 0x05125DAD, 0x1642AE59, 0xE4292D5A,
    0xBA3A117E, 0x4851927D, 0x5B016189, 0xA96AE28A,
    0x7DA08661, 0x8FCB0562, 0x9C9BF696, 0x6EF07595,
    0x417B1DBC, 0xB3109EBF, 0xA0406D4B, 0x522BEE48,
    0x86E18AA3, 0x748A09A0, 0x67DAFA54, 0x95B17957,
    0xCBA24573, 0x39C9C670, 0x2A993584, 0xD8F2B687,
    0x0C38D26C, 0xFE53516F, 0xED03A29B, 0x1F682198,
    0x5125DAD3, 0xA34E59D0, 0xB01EAA24, 0x42752927,
    0x96BF4DCC, 0x64D4CECF, 0x77843D3B, 0x85EFBE38,
    0xDBFC821C, 0x2997011F, 0x3AC7F2EB, 0xC8AC71E8,
    0x1C661503, 0xEE0D9600, 0xFD5D65F4, 0x0F36E6F7,
    0x61C69362, 0x93AD1061, 0x80FDE395, 0x72966096,
    0xA65C047D, 0x5437877E, 0x4767748A, 0xB50CF789,
    0xEB1FCBAD, 0x197448AE, 0x0A24BB5A, 0xF84F3859,
    0x2C855CB2, 0xDEEEDFB1, 0xCDBE2C45, 0x3FD5AF46,
    0x7198540D, 0x83F3D70E, 0x90A324FA, 0x62C8A7F9,
    0xB602C312, 0x44694011, 0x5739B3E5, 0xA55230E6,
    0xFB410CC2, 0x092A8FC1, 0x1A7A7C35, 0xE811FF36,
    0x3CDB9BDD, 0xCEB018DE, 0xDDE0EB2A, 0x2F8B6829,
    0x82F63B78, 0x709DB87B, 0x63CD4B8F, 0x91A6C88C,
    0x456CAC67, 0xB7072F64, 0xA457DC90, 0x563C5F93,
    0x082F63B7, 0xFA44E0B4, 0xE9141340, 0x1B7F9043,
    0xCFB5F4A8, 0x3DDE77AB, 0x2E8E845F, 0xDCE5075C,
    0x92A8FC17, 0x60C37F14, 0x73938CE0, 0x81F80FE3,
    0x55326B08, 0xA759E80B, 0xB4091BFF, 0x466298FC,
    0x1871A4D8, 0xEA1A27DB, 0xF94AD42F, 0x0B21572C,
    0xDFEB33C7, 0x2D80B0C4, 0x3ED04330, 0xCCBBC033,
    0xA24BB5A6, 0x502036A5, 0x4370C551, 0xB11B4652,
    0x65D122B9, 0x97BAA1BA, 0x84EA524E, 0x76810D4D,
    0x28920169, 0xDAF9826A, 0xC9A9719E, 0x3BC2F29D,
    0xEF089676, 0x1D631575, 0x0E33E681, 0xFC586582,
    0xB2159EC9, 0x407E1DCA, 0x532EEE3E, 0xA1456D3D,
    0x75CF09D6, 0x87A48AD5, 0x94F47921, 0x669FFA22,
    0x388CC606, 0xCAE74505, 0xD9B7B6F1, 0x2BDC35F2,
    0xFF165119, 0x0D7DD21A, 0x1E2D21EE, 0xEC46A2ED,
    0xC3CD6A04, 0x31A6E907, 0x22F61AF3, 0xD09D99F0,
    0x0457FD1B, 0xF63C7E18, 0xE56C8DEC, 0x17070EEF,
    0x491435CB, 0xBB7FB6C8, 0xA82F453C, 0x5A44C63F,
    0x8E8EA2D4, 0x7CE521D7, 0x6FB5D223, 0x9DDE5120,
    0xD393AA6B, 0x21F82968, 0x32A8DA9C, 0xC0C3599F,
    0x14093D74, 0xE662BE77, 0xF5324D83, 0x071DCE80,
    0x590EF2A4, 0xAB6571A7, 0xB8358253, 0x4A5E0150,
    0x9E9465BB, 0x6CFFE6B8, 0x7FAF154C, 0x8DC4964F,
    0xE3346B7E, 0x115FE87D, 0x020F1B89, 0xF064988A,
    0x24AEFC61, 0xD6C57F62, 0xC5958C96, 0x37FE0F95,
    0x69ED33B1, 0x9B86B0B2, 0x88D64346, 0x7ABDC045,
    0xAE77A4AE, 0x5C1C27AD, 0x4F4CD459, 0xBD27575A,
    0xF36AAC11, 0x01012F12, 0x1251DCE6, 0xE03A5FE5,
    0x34F03B0E, 0xC69BB80D, 0xD5CB4BF9, 0x27A0C8FA,
    0x79B3F4DE, 0x8BD877DD, 0x98888429, 0x6AE3072A,
    0xBE2963C1, 0x4C42E0C2, 0x5F121336, 0xAD799035
};

static ULONG
VhdxCrc32c(
    const void *Data,
    ULONG Length)
{
    const unsigned char *Bytes = (const unsigned char *)Data;
    ULONG Crc = 0xFFFFFFFF;
    ULONG i;

    for (i = 0; i < Length; i++)
    {
        Crc = Crc32cTable[(Crc ^ Bytes[i]) & 0xFF] ^ (Crc >> 8);
    }

    return Crc ^ 0xFFFFFFFF;
}

/* ========================================================================== */
/* Logging helper                                                              */
/* ========================================================================== */

static void Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[vhdxtool] ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static void LogError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[vhdxtool] ERROR: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ========================================================================== */
/* GUID helpers                                                                */
/* ========================================================================== */

static void
GenerateGuid(GUID *g)
{
    /* Use Win32 CoCreateGuid or UuidCreate.
     * UuidCreate is in rpcrt4.dll. For simplicity, use random bytes. */
    HMODULE hRpcrt4 = LoadLibraryA("rpcrt4.dll");
    if (hRpcrt4)
    {
        typedef long (WINAPI *PFN_UuidCreate)(GUID *);
        PFN_UuidCreate pfn = (PFN_UuidCreate)GetProcAddress(hRpcrt4, "UuidCreate");
        if (pfn)
        {
            pfn(g);
            FreeLibrary(hRpcrt4);
            return;
        }
        FreeLibrary(hRpcrt4);
    }

    /* Fallback: fill with pseudo-random bytes using GetTickCount + counter */
    {
        ULONG *p = (ULONG *)g;
        ULONG seed = GetTickCount() ^ (ULONG)(ULONG_PTR)g;
        int i;
        for (i = 0; i < 4; i++)
        {
            seed = seed * 1103515245 + 12345;
            p[i] = seed;
        }
        /* Set version 4 (random) and variant bits */
        g->Data3 = (g->Data3 & 0x0FFF) | 0x4000;
        g->Data4[0] = (g->Data4[0] & 0x3F) | 0x80;
    }
}

static void
FormatGuid(const GUID *g, char *buf, int bufsize)
{
    _snprintf(buf, bufsize,
              "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
              g->Data1, g->Data2, g->Data3,
              g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
              g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static BOOL
IsNullGuid(const GUID *g)
{
    static const GUID NullGuid = { 0 };
    return memcmp(g, &NullGuid, sizeof(GUID)) == 0;
}

/* ========================================================================== */
/* File I/O helpers                                                            */
/* ========================================================================== */

static BOOL
WriteAt(HANDLE hFile, ULONG64 Offset, const void *Data, ULONG Size)
{
    LARGE_INTEGER li;
    DWORD written;

    li.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN))
    {
        LogError("SetFilePointerEx to 0x%llx failed (error %lu)",
                 (unsigned long long)Offset, GetLastError());
        return FALSE;
    }
    if (!WriteFile(hFile, Data, Size, &written, NULL) || written != Size)
    {
        LogError("WriteFile of %lu bytes at 0x%llx failed (error %lu, wrote %lu)",
                 Size, (unsigned long long)Offset, GetLastError(), written);
        return FALSE;
    }
    return TRUE;
}

static BOOL
ReadAt(HANDLE hFile, ULONG64 Offset, void *Data, ULONG Size)
{
    LARGE_INTEGER li;
    DWORD bytesRead;

    li.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN))
    {
        LogError("SetFilePointerEx to 0x%llx failed (error %lu)",
                 (unsigned long long)Offset, GetLastError());
        return FALSE;
    }
    if (!ReadFile(hFile, Data, Size, &bytesRead, NULL) || bytesRead != Size)
    {
        LogError("ReadFile of %lu bytes at 0x%llx failed (error %lu, read %lu)",
                 Size, (unsigned long long)Offset, GetLastError(), bytesRead);
        return FALSE;
    }
    return TRUE;
}

static BOOL
WriteZerosAt(HANDLE hFile, ULONG64 Offset, ULONG Size)
{
    unsigned char zeros[4096];
    ULONG remaining = Size;
    ULONG64 pos = Offset;

    memset(zeros, 0, sizeof(zeros));
    while (remaining > 0)
    {
        ULONG chunk = remaining > sizeof(zeros) ? sizeof(zeros) : remaining;
        if (!WriteAt(hFile, pos, zeros, chunk))
            return FALSE;
        pos += chunk;
        remaining -= chunk;
    }
    return TRUE;
}

static BOOL
SetFileSize(HANDLE hFile, ULONG64 Size)
{
    LARGE_INTEGER li;

    li.QuadPart = (LONGLONG)Size;
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN))
    {
        LogError("SetFilePointerEx to 0x%llx failed (error %lu)",
                 (unsigned long long)Size, GetLastError());
        return FALSE;
    }
    if (!SetEndOfFile(hFile))
    {
        LogError("SetEndOfFile at 0x%llx failed (error %lu)",
                 (unsigned long long)Size, GetLastError());
        return FALSE;
    }
    return TRUE;
}

/* ========================================================================== */
/* Size parsing                                                                */
/* ========================================================================== */

static BOOL
ParseSize(const char *str, ULONG64 *outBytes)
{
    char *endp;
    ULONG64 val = _strtoui64(str, &endp, 10);
    if (endp == str)
        return FALSE;

    switch (*endp)
    {
    case 'G': case 'g':
        val *= 1024ULL * 1024 * 1024;
        break;
    case 'M': case 'm':
        val *= 1024ULL * 1024;
        break;
    case 'K': case 'k':
        val *= 1024ULL;
        break;
    case 'T': case 't':
        val *= 1024ULL * 1024 * 1024 * 1024;
        break;
    case '\0':
        /* raw bytes */
        break;
    default:
        return FALSE;
    }

    *outBytes = val;
    return TRUE;
}

/* ========================================================================== */
/* VHDX structure computation helpers                                          */
/* ========================================================================== */

#define VHDX_LOG_OFFSET         (1ULL * 1024 * 1024)       /* 1 MB */
#define VHDX_LOG_SIZE           (1UL * 1024 * 1024)        /* 1 MB */
#define VHDX_BAT_OFFSET         (2ULL * 1024 * 1024)       /* 2 MB */
#define VHDX_METADATA_OFFSET    (3ULL * 1024 * 1024)       /* 3 MB */

/* Metadata items are placed after the 64KB metadata table header area.
 * Spec says offsets must be > 64KB from metadata region start. */
#define VHDX_META_ITEMS_OFFSET  (64UL * 1024)              /* 64 KB into metadata region */

static ULONG
CalcChunkRatio(ULONG BlockSize, ULONG LogicalSectorSize)
{
    return (ULONG)((8ULL * 1024 * 1024 * (ULONG64)LogicalSectorSize) / BlockSize);
}

static ULONG
CalcDataBlocksCount(ULONG64 VirtualDiskSize, ULONG BlockSize)
{
    return (ULONG)((VirtualDiskSize + BlockSize - 1) / BlockSize);
}

static ULONG
CalcTotalBatEntries(ULONG DataBlocksCount, ULONG ChunkRatio)
{
    /* Total = DataBlocksCount + floor((DataBlocksCount - 1) / ChunkRatio)
     * The floor term accounts for interleaved sector bitmap entries. */
    if (DataBlocksCount == 0)
        return 0;
    return DataBlocksCount + (DataBlocksCount - 1) / ChunkRatio;
}

static ULONG
CalcBatSizeBytes(ULONG TotalBatEntries)
{
    /* Each entry is 8 bytes. Round up to 1MB alignment. */
    ULONG raw = TotalBatEntries * 8;
    return (raw + VHDX_MB_ALIGNMENT - 1) & ~(VHDX_MB_ALIGNMENT - 1);
}

/* ========================================================================== */
/* Write VHDX file structure                                                   */
/* ========================================================================== */

typedef struct _VHDX_CREATE_PARAMS {
    ULONG64     VirtualDiskSize;
    ULONG       BlockSize;
    ULONG       LogicalSectorSize;
    ULONG       PhysicalSectorSize;
    BOOL        IsFixed;
    GUID        FileWriteGuid;
    GUID        DataWriteGuid;
    GUID        VirtualDiskId;
} VHDX_CREATE_PARAMS;

static BOOL
WriteFileIdentifier(HANDLE hFile)
{
    VHDX_FILE_IDENTIFIER FileId;
    static const WCHAR Creator[] = L"ROSV vhdxtool";

    Log("Writing file identifier at offset 0x0...");
    memset(&FileId, 0, sizeof(FileId));
    FileId.Signature = VHDX_FILE_SIGNATURE;
    memcpy(FileId.Creator, Creator, sizeof(Creator));

    /* Write the 520-byte structure, then zero-fill to 64KB */
    if (!WriteAt(hFile, VHDX_FILE_ID_OFFSET, &FileId, sizeof(FileId)))
        return FALSE;
    if (!WriteZerosAt(hFile, VHDX_FILE_ID_OFFSET + sizeof(FileId),
                      VHDX_FILE_ID_SIZE - sizeof(FileId)))
        return FALSE;

    Log("  Signature: 0x%016llx (\"vhdxfile\")", (unsigned long long)VHDX_FILE_SIGNATURE);
    Log("  Creator: \"ROSV vhdxtool\"");
    return TRUE;
}

static BOOL
WriteHeaders(HANDLE hFile, const VHDX_CREATE_PARAMS *Params)
{
    VHDX_HEADER Header;
    ULONG Checksum;
    char guidBuf[64];

    Log("Writing Header 1 at offset 0x%llx...", (unsigned long long)VHDX_HEADER1_OFFSET);

    memset(&Header, 0, sizeof(Header));
    Header.Signature = VHDX_HEADER_SIGNATURE;
    Header.SequenceNumber = 1;
    Header.FileWriteGuid = Params->FileWriteGuid;
    Header.DataWriteGuid = Params->DataWriteGuid;
    /* LogGuid = GUID_NULL (log is clean for a new file) */
    Header.LogVersion = VHDX_LOG_VERSION;
    Header.Version = VHDX_FORMAT_VERSION;
    Header.LogLength = VHDX_LOG_SIZE;
    Header.LogOffset = VHDX_LOG_OFFSET;
    Header.Checksum = 0;

    /* Compute CRC-32C over the 4KB header structure */
    Checksum = VhdxCrc32c(&Header, sizeof(Header));
    Header.Checksum = Checksum;

    FormatGuid(&Header.FileWriteGuid, guidBuf, sizeof(guidBuf));
    Log("  Signature: 0x%08lx (\"head\")", Header.Signature);
    Log("  Version: %u, LogVersion: %u", Header.Version, Header.LogVersion);
    Log("  SequenceNumber: %llu", (unsigned long long)Header.SequenceNumber);
    Log("  FileWriteGuid: %s", guidBuf);
    FormatGuid(&Header.DataWriteGuid, guidBuf, sizeof(guidBuf));
    Log("  DataWriteGuid: %s", guidBuf);
    Log("  LogOffset: 0x%llx, LogLength: 0x%lx (%lu MB)",
        (unsigned long long)Header.LogOffset, Header.LogLength,
        Header.LogLength / (1024 * 1024));
    Log("  Checksum: 0x%08lx", Header.Checksum);

    /* Write Header 1 (4KB struct + zero fill to 64KB) */
    if (!WriteAt(hFile, VHDX_HEADER1_OFFSET, &Header, sizeof(Header)))
        return FALSE;
    if (!WriteZerosAt(hFile, VHDX_HEADER1_OFFSET + sizeof(Header),
                      VHDX_HEADER_SIZE - sizeof(Header)))
        return FALSE;

    /* Write Header 2 (identical copy with same sequence number) */
    Log("Writing Header 2 at offset 0x%llx (copy of Header 1)...",
        (unsigned long long)VHDX_HEADER2_OFFSET);
    if (!WriteAt(hFile, VHDX_HEADER2_OFFSET, &Header, sizeof(Header)))
        return FALSE;
    if (!WriteZerosAt(hFile, VHDX_HEADER2_OFFSET + sizeof(Header),
                      VHDX_HEADER_SIZE - sizeof(Header)))
        return FALSE;

    return TRUE;
}

static BOOL
WriteRegionTables(HANDLE hFile, ULONG BatLength)
{
    /* Region table = 16-byte header + 2 entries (BAT + Metadata) = 80 bytes.
     * CRC-32C computed over the entire 64KB region. */
    unsigned char *RegionBuf;
    VHDX_REGION_TABLE_HEADER *Hdr;
    VHDX_REGION_TABLE_ENTRY *Entry;
    ULONG Checksum;
    static const GUID BatGuid = VHDX_BAT_REGION_GUID;
    static const GUID MetaGuid = VHDX_METADATA_REGION_GUID;

    Log("Writing Region Table 1 at offset 0x%llx...",
        (unsigned long long)VHDX_REGION_TABLE1_OFFSET);

    RegionBuf = (unsigned char *)calloc(1, VHDX_REGION_TABLE_SIZE);
    if (!RegionBuf)
    {
        LogError("Failed to allocate region table buffer");
        return FALSE;
    }

    Hdr = (VHDX_REGION_TABLE_HEADER *)RegionBuf;
    Hdr->Signature = VHDX_REGION_SIGNATURE;
    Hdr->EntryCount = 2;
    Hdr->Reserved = 0;
    Hdr->Checksum = 0;

    /* Entry 0: BAT region */
    Entry = (VHDX_REGION_TABLE_ENTRY *)(RegionBuf + sizeof(VHDX_REGION_TABLE_HEADER));
    Entry->Guid = BatGuid;
    Entry->FileOffset = VHDX_BAT_OFFSET;
    Entry->Length = BatLength;
    Entry->Required = 1;

    Log("  Entry 0: BAT region at 0x%llx, length 0x%lx (%lu MB)",
        (unsigned long long)Entry->FileOffset, Entry->Length,
        Entry->Length / (1024 * 1024));

    /* Entry 1: Metadata region */
    Entry = (VHDX_REGION_TABLE_ENTRY *)(RegionBuf + sizeof(VHDX_REGION_TABLE_HEADER)
             + sizeof(VHDX_REGION_TABLE_ENTRY));
    Entry->Guid = MetaGuid;
    Entry->FileOffset = VHDX_METADATA_OFFSET;
    Entry->Length = VHDX_MB_ALIGNMENT; /* 1 MB */
    Entry->Required = 1;

    Log("  Entry 1: Metadata region at 0x%llx, length 0x%lx (%lu MB)",
        (unsigned long long)Entry->FileOffset, Entry->Length,
        Entry->Length / (1024 * 1024));

    /* Compute CRC-32C over the full 64KB region table */
    Checksum = VhdxCrc32c(RegionBuf, VHDX_REGION_TABLE_SIZE);
    Hdr->Checksum = Checksum;
    Log("  Checksum: 0x%08lx", Checksum);

    /* Write Region Table 1 */
    if (!WriteAt(hFile, VHDX_REGION_TABLE1_OFFSET, RegionBuf, VHDX_REGION_TABLE_SIZE))
    {
        free(RegionBuf);
        return FALSE;
    }

    /* Write Region Table 2 (identical copy) */
    Log("Writing Region Table 2 at offset 0x%llx (copy of Region Table 1)...",
        (unsigned long long)VHDX_REGION_TABLE2_OFFSET);
    if (!WriteAt(hFile, VHDX_REGION_TABLE2_OFFSET, RegionBuf, VHDX_REGION_TABLE_SIZE))
    {
        free(RegionBuf);
        return FALSE;
    }

    free(RegionBuf);
    return TRUE;
}

/* Helper: determine the BAT array index for payload block N.
 * BAT layout interleaves sector bitmap entries every chunk_ratio payload entries:
 *   [P0, P1, ..., P(CR-1), SB0, P(CR), ..., P(2*CR-1), SB1, ...]
 * So bat_index(N) = N + floor(N / chunk_ratio).
 * SB entries are at indices: (CR+1)*k + CR  for k=0,1,2,...
 */
static ULONG
PayloadBatIndex(ULONG BlockN, ULONG ChunkRatio)
{
    return BlockN + (BlockN / ChunkRatio);
}

static BOOL
IsSectorBitmapIndex(ULONG BatIdx, ULONG ChunkRatio)
{
    /* SB entry positions: every (ChunkRatio+1) entries, at offset ChunkRatio.
     * i.e. BatIdx % (ChunkRatio + 1) == ChunkRatio */
    if (ChunkRatio == 0)
        return FALSE;
    return ((BatIdx % (ChunkRatio + 1)) == ChunkRatio);
}

static BOOL
WriteBat(HANDLE hFile, ULONG TotalBatEntries, ULONG DataBlocksCount,
         ULONG ChunkRatio, BOOL IsFixed, ULONG BlockSize)
{
    ULONG64 *BatArray;
    ULONG BatSizeBytes;
    ULONG n;
    ULONG64 NextOffsetMB;
    ULONG PresentCount = 0;

    BatSizeBytes = TotalBatEntries * 8;
    Log("Writing BAT at offset 0x%llx (%lu entries, %lu bytes)...",
        (unsigned long long)VHDX_BAT_OFFSET, TotalBatEntries, BatSizeBytes);

    BatArray = (ULONG64 *)calloc(TotalBatEntries, sizeof(ULONG64));
    if (!BatArray)
    {
        LogError("Failed to allocate BAT array (%lu entries)", TotalBatEntries);
        return FALSE;
    }

    if (IsFixed)
    {
        /* Fixed disk: all payload blocks are FULLY_PRESENT with sequential offsets.
         * Payload data starts after metadata region (at 4MB for our layout). */
        ULONG64 PayloadStart = VHDX_METADATA_OFFSET + VHDX_MB_ALIGNMENT;
        NextOffsetMB = PayloadStart >> 20;

        Log("  Fixed disk: pre-allocating %lu payload blocks starting at 0x%llx",
            DataBlocksCount, (unsigned long long)PayloadStart);

        for (n = 0; n < DataBlocksCount; n++)
        {
            ULONG BatIdx = PayloadBatIndex(n, ChunkRatio);
            BatArray[BatIdx] = VHDX_BAT_MAKE_ENTRY(VHDX_PAYLOAD_BLOCK_FULLY_PRESENT,
                                                     NextOffsetMB);
            NextOffsetMB += (ULONG64)BlockSize >> 20;
            PresentCount++;
        }
        /* SB entries and unused payload entries remain zero (NOT_PRESENT) */
        Log("  %lu blocks set to FULLY_PRESENT", PresentCount);
    }
    else
    {
        /* Dynamic disk: all entries are NOT_PRESENT (zeros) */
        Log("  Dynamic disk: all entries set to NOT_PRESENT (zeros)");
    }

    if (!WriteAt(hFile, VHDX_BAT_OFFSET, BatArray, BatSizeBytes))
    {
        free(BatArray);
        return FALSE;
    }

    free(BatArray);
    return TRUE;
}

static BOOL
WriteMetadata(HANDLE hFile, const VHDX_CREATE_PARAMS *Params)
{
    /* Metadata table header + 5 entries = 32 + 5*32 = 192 bytes.
     * Actual metadata items start at VHDX_META_ITEMS_OFFSET (64KB) within the region. */
    unsigned char *MetaBuf;
    VHDX_METADATA_TABLE_HEADER *Hdr;
    VHDX_METADATA_TABLE_ENTRY *Entry;
    ULONG ItemOffset;
    ULONG MetaRegionSize;
    char guidBuf[64];

    static const GUID FileParamsGuid = VHDX_META_FILE_PARAMETERS_GUID;
    static const GUID VirtDiskSizeGuid = VHDX_META_VIRTUAL_DISK_SIZE_GUID;
    static const GUID LogSectorGuid = VHDX_META_LOGICAL_SECTOR_SIZE_GUID;
    static const GUID PhysSectorGuid = VHDX_META_PHYSICAL_SECTOR_SIZE_GUID;
    static const GUID VirtDiskIdGuid = VHDX_META_VIRTUAL_DISK_ID_GUID;

    Log("Writing Metadata at offset 0x%llx...",
        (unsigned long long)VHDX_METADATA_OFFSET);

    /* Allocate a buffer for the entire metadata region (1 MB) */
    MetaRegionSize = VHDX_MB_ALIGNMENT;
    MetaBuf = (unsigned char *)calloc(1, MetaRegionSize);
    if (!MetaBuf)
    {
        LogError("Failed to allocate metadata buffer");
        return FALSE;
    }

    /* Metadata table header */
    Hdr = (VHDX_METADATA_TABLE_HEADER *)MetaBuf;
    Hdr->Signature = VHDX_METADATA_SIGNATURE;
    Hdr->EntryCount = 5;

    /* Items are placed sequentially starting at VHDX_META_ITEMS_OFFSET */
    ItemOffset = VHDX_META_ITEMS_OFFSET;

    /* Entry 0: File Parameters (8 bytes) */
    Entry = (VHDX_METADATA_TABLE_ENTRY *)(MetaBuf + sizeof(VHDX_METADATA_TABLE_HEADER));
    Entry->ItemId = FileParamsGuid;
    Entry->Offset = ItemOffset;
    Entry->Length = sizeof(VHDX_FILE_PARAMETERS);
    Entry->Flags = VHDX_METADATA_FLAG_IS_REQUIRED;

    {
        VHDX_FILE_PARAMETERS *fp = (VHDX_FILE_PARAMETERS *)(MetaBuf + ItemOffset);
        fp->BlockSize = Params->BlockSize;
        fp->Flags = Params->IsFixed ? VHDX_FILE_PARAMS_LEAVE_BLOCKS_ALLOCATED : 0;
        Log("  FileParameters: BlockSize=%lu (0x%lx), Flags=0x%lx (%s)",
            fp->BlockSize, fp->BlockSize, fp->Flags,
            Params->IsFixed ? "fixed" : "dynamic");
    }
    ItemOffset += sizeof(VHDX_FILE_PARAMETERS);
    /* Align to 8 bytes */
    ItemOffset = (ItemOffset + 7) & ~7UL;

    /* Entry 1: Virtual Disk Size (8 bytes) */
    Entry = (VHDX_METADATA_TABLE_ENTRY *)(MetaBuf + sizeof(VHDX_METADATA_TABLE_HEADER)
             + 1 * sizeof(VHDX_METADATA_TABLE_ENTRY));
    Entry->ItemId = VirtDiskSizeGuid;
    Entry->Offset = ItemOffset;
    Entry->Length = 8;
    Entry->Flags = VHDX_METADATA_FLAG_IS_REQUIRED | VHDX_METADATA_FLAG_IS_VIRTUAL_DISK;

    *(ULONG64 *)(MetaBuf + ItemOffset) = Params->VirtualDiskSize;
    Log("  VirtualDiskSize: %llu bytes (%llu MB)",
        (unsigned long long)Params->VirtualDiskSize,
        (unsigned long long)(Params->VirtualDiskSize / (1024 * 1024)));
    ItemOffset += 8;

    /* Entry 2: Logical Sector Size (4 bytes) */
    Entry = (VHDX_METADATA_TABLE_ENTRY *)(MetaBuf + sizeof(VHDX_METADATA_TABLE_HEADER)
             + 2 * sizeof(VHDX_METADATA_TABLE_ENTRY));
    Entry->ItemId = LogSectorGuid;
    Entry->Offset = ItemOffset;
    Entry->Length = 4;
    Entry->Flags = VHDX_METADATA_FLAG_IS_REQUIRED | VHDX_METADATA_FLAG_IS_VIRTUAL_DISK;

    *(ULONG *)(MetaBuf + ItemOffset) = Params->LogicalSectorSize;
    Log("  LogicalSectorSize: %lu", Params->LogicalSectorSize);
    ItemOffset += 4;
    ItemOffset = (ItemOffset + 7) & ~7UL;

    /* Entry 3: Physical Sector Size (4 bytes) */
    Entry = (VHDX_METADATA_TABLE_ENTRY *)(MetaBuf + sizeof(VHDX_METADATA_TABLE_HEADER)
             + 3 * sizeof(VHDX_METADATA_TABLE_ENTRY));
    Entry->ItemId = PhysSectorGuid;
    Entry->Offset = ItemOffset;
    Entry->Length = 4;
    Entry->Flags = VHDX_METADATA_FLAG_IS_REQUIRED | VHDX_METADATA_FLAG_IS_VIRTUAL_DISK;

    *(ULONG *)(MetaBuf + ItemOffset) = Params->PhysicalSectorSize;
    Log("  PhysicalSectorSize: %lu", Params->PhysicalSectorSize);
    ItemOffset += 4;
    ItemOffset = (ItemOffset + 7) & ~7UL;

    /* Entry 4: Virtual Disk ID (16 bytes = GUID) */
    Entry = (VHDX_METADATA_TABLE_ENTRY *)(MetaBuf + sizeof(VHDX_METADATA_TABLE_HEADER)
             + 4 * sizeof(VHDX_METADATA_TABLE_ENTRY));
    Entry->ItemId = VirtDiskIdGuid;
    Entry->Offset = ItemOffset;
    Entry->Length = sizeof(GUID);
    Entry->Flags = VHDX_METADATA_FLAG_IS_REQUIRED | VHDX_METADATA_FLAG_IS_VIRTUAL_DISK;

    *(GUID *)(MetaBuf + ItemOffset) = Params->VirtualDiskId;
    FormatGuid(&Params->VirtualDiskId, guidBuf, sizeof(guidBuf));
    Log("  VirtualDiskId: %s", guidBuf);

    /* Write the entire metadata region */
    if (!WriteAt(hFile, VHDX_METADATA_OFFSET, MetaBuf, MetaRegionSize))
    {
        free(MetaBuf);
        return FALSE;
    }

    free(MetaBuf);
    return TRUE;
}

/* ========================================================================== */
/* "create" subcommand                                                         */
/* ========================================================================== */

static int
CmdCreate(int argc, char **argv)
{
    const char *OutputPath = NULL;
    const char *TypeStr = "dynamic";
    const char *SizeStr = NULL;
    const char *BlockSizeStr = NULL;
    BOOL IsFixed = FALSE;
    ULONG64 VirtualDiskSize = 0;
    ULONG BlockSize = VHDX_BLOCK_SIZE_MIN; /* Default 1 MB */
    ULONG LogicalSectorSize = 512;
    ULONG PhysicalSectorSize = 512;
    ULONG DataBlocksCount;
    ULONG ChunkRatio;
    ULONG TotalBatEntries;
    ULONG BatSizeBytes;
    ULONG64 TotalFileSize;
    HANDLE hFile;
    VHDX_CREATE_PARAMS Params;
    int i;

    /* Parse arguments */
    for (i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "--type") == 0 && i + 1 < argc)
        {
            TypeStr = argv[++i];
        }
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
        {
            SizeStr = argv[++i];
        }
        else if (strcmp(argv[i], "--block-size") == 0 && i + 1 < argc)
        {
            BlockSizeStr = argv[++i];
        }
        else if (argv[i][0] != '-')
        {
            OutputPath = argv[i];
        }
    }

    if (!SizeStr || !OutputPath)
    {
        LogError("Usage: vhdxtool create --type dynamic|fixed --size <N>G [--block-size <N>M] output.vhdx");
        return 1;
    }

    if (!ParseSize(SizeStr, &VirtualDiskSize) || VirtualDiskSize == 0)
    {
        LogError("Invalid size: '%s'", SizeStr);
        return 1;
    }

    if (_stricmp(TypeStr, "fixed") == 0)
        IsFixed = TRUE;
    else if (_stricmp(TypeStr, "dynamic") == 0)
        IsFixed = FALSE;
    else
    {
        LogError("Invalid type: '%s' (expected 'dynamic' or 'fixed')", TypeStr);
        return 1;
    }

    if (BlockSizeStr)
    {
        ULONG64 bs;
        if (!ParseSize(BlockSizeStr, &bs))
        {
            LogError("Invalid block size: '%s'", BlockSizeStr);
            return 1;
        }
        BlockSize = (ULONG)bs;
    }

    /* Validate block size */
    if (BlockSize < VHDX_BLOCK_SIZE_MIN || BlockSize > VHDX_BLOCK_SIZE_MAX)
    {
        LogError("Block size %lu is out of range [%u, %u]",
                 BlockSize, VHDX_BLOCK_SIZE_MIN, VHDX_BLOCK_SIZE_MAX);
        return 1;
    }
    if ((BlockSize & (BlockSize - 1)) != 0)
    {
        LogError("Block size %lu is not a power of 2", BlockSize);
        return 1;
    }

    /* Compute layout */
    DataBlocksCount = CalcDataBlocksCount(VirtualDiskSize, BlockSize);
    ChunkRatio = CalcChunkRatio(BlockSize, LogicalSectorSize);
    TotalBatEntries = CalcTotalBatEntries(DataBlocksCount, ChunkRatio);
    BatSizeBytes = CalcBatSizeBytes(TotalBatEntries);

    Log("=== Creating %s VHDX ===", IsFixed ? "fixed" : "dynamic");
    Log("  Virtual disk size: %llu bytes (%llu MB)",
        (unsigned long long)VirtualDiskSize,
        (unsigned long long)(VirtualDiskSize / (1024 * 1024)));
    Log("  Block size: %lu bytes (%lu MB)", BlockSize, BlockSize / (1024 * 1024));
    Log("  Data blocks: %lu", DataBlocksCount);
    Log("  Chunk ratio: %lu", ChunkRatio);
    Log("  Total BAT entries: %lu", TotalBatEntries);
    Log("  BAT region size: %lu bytes (%lu MB)", BatSizeBytes, BatSizeBytes / (1024 * 1024));
    Log("  Output: %s", OutputPath);

    /* Compute total file size */
    if (IsFixed)
    {
        ULONG64 PayloadStart = VHDX_METADATA_OFFSET + VHDX_MB_ALIGNMENT;
        TotalFileSize = PayloadStart + (ULONG64)DataBlocksCount * BlockSize;
    }
    else
    {
        /* Dynamic: file is just headers + log + BAT + metadata (small) */
        TotalFileSize = VHDX_METADATA_OFFSET + VHDX_MB_ALIGNMENT;
    }
    Log("  Total file size: %llu bytes (%llu MB)",
        (unsigned long long)TotalFileSize,
        (unsigned long long)(TotalFileSize / (1024 * 1024)));

    /* Generate GUIDs */
    memset(&Params, 0, sizeof(Params));
    Params.VirtualDiskSize = VirtualDiskSize;
    Params.BlockSize = BlockSize;
    Params.LogicalSectorSize = LogicalSectorSize;
    Params.PhysicalSectorSize = PhysicalSectorSize;
    Params.IsFixed = IsFixed;
    GenerateGuid(&Params.FileWriteGuid);
    GenerateGuid(&Params.DataWriteGuid);
    GenerateGuid(&Params.VirtualDiskId);

    /* Create output file */
    hFile = CreateFileA(OutputPath, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        LogError("Failed to create '%s' (error %lu)", OutputPath, GetLastError());
        return 1;
    }

    /* Pre-allocate file to final size for fixed, or header+meta size for dynamic */
    Log("Pre-allocating file to %llu bytes...", (unsigned long long)TotalFileSize);
    if (!SetFileSize(hFile, TotalFileSize))
    {
        CloseHandle(hFile);
        DeleteFileA(OutputPath);
        return 1;
    }

    /* Write all VHDX structures */
    if (!WriteFileIdentifier(hFile))
        goto fail;
    if (!WriteHeaders(hFile, &Params))
        goto fail;
    if (!WriteRegionTables(hFile, BatSizeBytes))
        goto fail;

    /* Write log region (all zeros = clean log) */
    Log("Writing log region at 0x%llx (%lu bytes, all zeros = clean)...",
        (unsigned long long)VHDX_LOG_OFFSET, VHDX_LOG_SIZE);
    /* File is already zero-filled from SetEndOfFile, but let's be explicit
     * for the log area in case the filesystem doesn't zero-fill. */
    if (!WriteZerosAt(hFile, VHDX_LOG_OFFSET, VHDX_LOG_SIZE))
        goto fail;

    if (!WriteBat(hFile, TotalBatEntries, DataBlocksCount, ChunkRatio,
                  IsFixed, BlockSize))
        goto fail;
    if (!WriteMetadata(hFile, &Params))
        goto fail;

    /* For fixed disk, zero-fill the payload blocks.
     * The file was pre-allocated to the right size, so the data area
     * is already present (and zero on NTFS). We just need the BAT
     * entries to point there, which WriteBat handled. */
    if (IsFixed)
    {
        ULONG64 PayloadStart = VHDX_METADATA_OFFSET + VHDX_MB_ALIGNMENT;
        Log("Fixed disk: payload data region at 0x%llx, size %llu MB",
            (unsigned long long)PayloadStart,
            (unsigned long long)((ULONG64)DataBlocksCount * BlockSize / (1024 * 1024)));
        Log("  (File pre-allocated, payload blocks are zero-filled)");
    }

    CloseHandle(hFile);
    Log("=== VHDX creation complete: %s ===", OutputPath);
    return 0;

fail:
    CloseHandle(hFile);
    DeleteFileA(OutputPath);
    LogError("VHDX creation failed, deleted incomplete file");
    return 1;
}

/* ========================================================================== */
/* "convert" subcommand                                                        */
/* ========================================================================== */

static BOOL
IsBlockAllZeros(const unsigned char *Data, ULONG Size)
{
    ULONG i;
    /* Check in ULONG64-sized chunks for speed */
    const ULONG64 *p64 = (const ULONG64 *)Data;
    ULONG count64 = Size / sizeof(ULONG64);

    for (i = 0; i < count64; i++)
    {
        if (p64[i] != 0)
            return FALSE;
    }
    /* Check remaining bytes */
    for (i = count64 * sizeof(ULONG64); i < Size; i++)
    {
        if (Data[i] != 0)
            return FALSE;
    }
    return TRUE;
}

static int
CmdConvert(int argc, char **argv)
{
    const char *InputPath = NULL;
    const char *OutputPath = NULL;
    const char *TypeStr = "dynamic";
    BOOL IsFixed = FALSE;
    HANDLE hInput;
    HANDLE hOutput;
    LARGE_INTEGER InputFileSize;
    ULONG64 VirtualDiskSize;
    ULONG BlockSize = VHDX_BLOCK_SIZE_MIN; /* 1 MB default for conversions */
    ULONG DataBlocksCount;
    ULONG ChunkRatio;
    ULONG TotalBatEntries;
    ULONG BatSizeBytes;
    ULONG64 TotalFileSize;
    VHDX_CREATE_PARAMS Params;
    unsigned char *BlockBuf = NULL;
    ULONG64 *BatArray = NULL;
    ULONG i;
    ULONG PresentCount = 0;
    ULONG ZeroCount = 0;
    int positionalIdx = 0;

    /* Parse arguments */
    for (i = 0; i < (ULONG)argc; i++)
    {
        if (strcmp(argv[i], "--type") == 0 && (int)i + 1 < argc)
        {
            TypeStr = argv[++i];
        }
        else if (strcmp(argv[i], "--block-size") == 0 && (int)i + 1 < argc)
        {
            ULONG64 bs;
            i++;
            if (!ParseSize(argv[i], &bs))
            {
                LogError("Invalid block size: '%s'", argv[i]);
                return 1;
            }
            BlockSize = (ULONG)bs;
        }
        else if (argv[i][0] != '-')
        {
            if (positionalIdx == 0)
                InputPath = argv[i];
            else if (positionalIdx == 1)
                OutputPath = argv[i];
            positionalIdx++;
        }
    }

    if (!InputPath || !OutputPath)
    {
        LogError("Usage: vhdxtool convert input.img output.vhdx [--type dynamic|fixed] [--block-size <N>M]");
        return 1;
    }

    if (_stricmp(TypeStr, "fixed") == 0)
        IsFixed = TRUE;
    else if (_stricmp(TypeStr, "dynamic") == 0)
        IsFixed = FALSE;
    else
    {
        LogError("Invalid type: '%s'", TypeStr);
        return 1;
    }

    /* Validate block size */
    if (BlockSize < VHDX_BLOCK_SIZE_MIN || BlockSize > VHDX_BLOCK_SIZE_MAX)
    {
        LogError("Block size %lu is out of range [%u, %u]",
                 BlockSize, VHDX_BLOCK_SIZE_MIN, VHDX_BLOCK_SIZE_MAX);
        return 1;
    }
    if ((BlockSize & (BlockSize - 1)) != 0)
    {
        LogError("Block size %lu is not a power of 2", BlockSize);
        return 1;
    }

    /* Open input file */
    hInput = CreateFileA(InputPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hInput == INVALID_HANDLE_VALUE)
    {
        LogError("Failed to open input '%s' (error %lu)", InputPath, GetLastError());
        return 1;
    }

    if (!GetFileSizeEx(hInput, &InputFileSize))
    {
        LogError("Failed to get input file size (error %lu)", GetLastError());
        CloseHandle(hInput);
        return 1;
    }

    VirtualDiskSize = (ULONG64)InputFileSize.QuadPart;
    /* Round up to block size boundary */
    VirtualDiskSize = (VirtualDiskSize + BlockSize - 1) & ~((ULONG64)BlockSize - 1);

    Log("=== Converting raw image to %s VHDX ===", IsFixed ? "fixed" : "dynamic");
    Log("  Input: %s (%llu bytes)", InputPath, (unsigned long long)InputFileSize.QuadPart);
    Log("  Virtual disk size: %llu bytes (%llu MB, rounded up to block boundary)",
        (unsigned long long)VirtualDiskSize,
        (unsigned long long)(VirtualDiskSize / (1024 * 1024)));
    Log("  Block size: %lu bytes (%lu MB)", BlockSize, BlockSize / (1024 * 1024));

    /* Compute layout */
    DataBlocksCount = CalcDataBlocksCount(VirtualDiskSize, BlockSize);
    ChunkRatio = CalcChunkRatio(BlockSize, 512);
    TotalBatEntries = CalcTotalBatEntries(DataBlocksCount, ChunkRatio);
    BatSizeBytes = CalcBatSizeBytes(TotalBatEntries);

    Log("  Data blocks: %lu, BAT entries: %lu", DataBlocksCount, TotalBatEntries);

    /* Compute file size */
    {
        ULONG64 PayloadStart = VHDX_METADATA_OFFSET + VHDX_MB_ALIGNMENT;
        if (IsFixed)
        {
            TotalFileSize = PayloadStart + VirtualDiskSize;
        }
        else
        {
            /* For dynamic, we'll grow the file as we write non-zero blocks */
            TotalFileSize = PayloadStart; /* Start with just headers */
        }
    }

    /* Create output file */
    hOutput = CreateFileA(OutputPath, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOutput == INVALID_HANDLE_VALUE)
    {
        LogError("Failed to create output '%s' (error %lu)", OutputPath, GetLastError());
        CloseHandle(hInput);
        return 1;
    }

    /* Generate params */
    memset(&Params, 0, sizeof(Params));
    Params.VirtualDiskSize = VirtualDiskSize;
    Params.BlockSize = BlockSize;
    Params.LogicalSectorSize = 512;
    Params.PhysicalSectorSize = 512;
    Params.IsFixed = IsFixed;
    GenerateGuid(&Params.FileWriteGuid);
    GenerateGuid(&Params.DataWriteGuid);
    GenerateGuid(&Params.VirtualDiskId);

    /* Pre-allocate file */
    if (IsFixed)
    {
        Log("Pre-allocating file to %llu bytes...", (unsigned long long)TotalFileSize);
        if (!SetFileSize(hOutput, TotalFileSize))
            goto convert_fail;
    }
    else
    {
        /* For dynamic, set initial size to metadata end */
        if (!SetFileSize(hOutput, TotalFileSize))
            goto convert_fail;
    }

    /* Write file structure (same for both types) */
    if (!WriteFileIdentifier(hOutput))
        goto convert_fail;
    if (!WriteHeaders(hOutput, &Params))
        goto convert_fail;
    if (!WriteRegionTables(hOutput, BatSizeBytes))
        goto convert_fail;
    if (!WriteZerosAt(hOutput, VHDX_LOG_OFFSET, VHDX_LOG_SIZE))
        goto convert_fail;
    if (!WriteMetadata(hOutput, &Params))
        goto convert_fail;

    /* Now copy blocks and build BAT */
    BlockBuf = (unsigned char *)malloc(BlockSize);
    BatArray = (ULONG64 *)calloc(TotalBatEntries, sizeof(ULONG64));
    if (!BlockBuf || !BatArray)
    {
        LogError("Failed to allocate block buffer or BAT array");
        goto convert_fail;
    }

    {
        ULONG64 PayloadStart = VHDX_METADATA_OFFSET + VHDX_MB_ALIGNMENT;
        ULONG64 NextWriteOffset = PayloadStart;
        ULONG n;

        Log("Copying %lu blocks from input image...", DataBlocksCount);

        for (n = 0; n < DataBlocksCount; n++)
        {
            ULONG BatIdx = PayloadBatIndex(n, ChunkRatio);
            ULONG64 InputOffset = (ULONG64)n * BlockSize;
            ULONG BytesToRead = BlockSize;
            DWORD BytesRead = 0;
            LARGE_INTEGER li;

            memset(BlockBuf, 0, BlockSize);

            if (InputOffset < (ULONG64)InputFileSize.QuadPart)
            {
                ULONG64 Remaining = (ULONG64)InputFileSize.QuadPart - InputOffset;
                if (Remaining < BytesToRead)
                    BytesToRead = (ULONG)Remaining;

                li.QuadPart = (LONGLONG)InputOffset;
                SetFilePointerEx(hInput, li, NULL, FILE_BEGIN);
                ReadFile(hInput, BlockBuf, BytesToRead, &BytesRead, NULL);
            }

            if (IsFixed)
            {
                /* Fixed: always write the block */
                ULONG64 OffsetMB = NextWriteOffset >> 20;
                BatArray[BatIdx] = VHDX_BAT_MAKE_ENTRY(VHDX_PAYLOAD_BLOCK_FULLY_PRESENT,
                                                         OffsetMB);
                if (!WriteAt(hOutput, NextWriteOffset, BlockBuf, BlockSize))
                    goto convert_fail;
                NextWriteOffset += BlockSize;
                PresentCount++;
            }
            else
            {
                /* Dynamic: skip all-zero blocks */
                if (IsBlockAllZeros(BlockBuf, BlockSize))
                {
                    BatArray[BatIdx] = VHDX_BAT_MAKE_ENTRY(VHDX_PAYLOAD_BLOCK_ZERO, 0);
                    ZeroCount++;
                }
                else
                {
                    ULONG64 OffsetMB = NextWriteOffset >> 20;
                    BatArray[BatIdx] = VHDX_BAT_MAKE_ENTRY(VHDX_PAYLOAD_BLOCK_FULLY_PRESENT,
                                                             OffsetMB);
                    /* Extend file if needed */
                    if (NextWriteOffset + BlockSize > TotalFileSize)
                    {
                        TotalFileSize = NextWriteOffset + BlockSize;
                        if (!SetFileSize(hOutput, TotalFileSize))
                            goto convert_fail;
                    }
                    if (!WriteAt(hOutput, NextWriteOffset, BlockBuf, BlockSize))
                        goto convert_fail;
                    NextWriteOffset += BlockSize;
                    PresentCount++;
                }
            }

            /* Progress every 256 blocks */
            if ((n & 0xFF) == 0 && n > 0)
            {
                Log("  Progress: %lu / %lu blocks (%lu present, %lu zero)",
                    n, DataBlocksCount, PresentCount, ZeroCount);
            }
        }
    }

    Log("Block copy complete: %lu present, %lu zero, %lu total",
        PresentCount, ZeroCount, DataBlocksCount);

    /* Write the final BAT */
    Log("Writing final BAT with %lu entries...", TotalBatEntries);
    if (!WriteAt(hOutput, VHDX_BAT_OFFSET, BatArray, TotalBatEntries * 8))
        goto convert_fail;

    free(BlockBuf);
    free(BatArray);
    CloseHandle(hInput);
    CloseHandle(hOutput);

    Log("=== Conversion complete: %s ===", OutputPath);
    Log("  Final file size: %llu bytes (%llu MB)",
        (unsigned long long)TotalFileSize,
        (unsigned long long)(TotalFileSize / (1024 * 1024)));
    return 0;

convert_fail:
    if (BlockBuf) free(BlockBuf);
    if (BatArray) free(BatArray);
    CloseHandle(hInput);
    CloseHandle(hOutput);
    DeleteFileA(OutputPath);
    LogError("Conversion failed, deleted incomplete output file");
    return 1;
}

/* ========================================================================== */
/* "info" subcommand                                                           */
/* ========================================================================== */

static int
CmdInfo(int argc, char **argv)
{
    const char *InputPath;
    HANDLE hFile;
    LARGE_INTEGER FileSize;
    VHDX_FILE_IDENTIFIER FileId;
    VHDX_HEADER Header1, Header2;
    VHDX_HEADER *ActiveHeader;
    int ActiveHeaderIdx;
    unsigned char *RegionBuf = NULL;
    VHDX_REGION_TABLE_HEADER *RegHdr;
    VHDX_REGION_TABLE_ENTRY *RegEntry;
    ULONG64 BatOffset = 0, MetadataOffset = 0;
    ULONG BatLength = 0, MetadataLength = 0;
    unsigned char *MetaBuf = NULL;
    VHDX_METADATA_TABLE_HEADER *MetaHdr;
    VHDX_METADATA_TABLE_ENTRY *MetaEntry;
    ULONG64 VirtualDiskSize = 0;
    ULONG BlockSize = 0;
    ULONG LogicalSectorSize = 512;
    ULONG PhysicalSectorSize = 512;
    ULONG FileParamsFlags = 0;
    GUID VirtualDiskId = { 0 };
    BOOL FoundFileParams = FALSE;
    BOOL FoundVirtSize = FALSE;
    ULONG DataBlocksCount, ChunkRatio, TotalBatEntries;
    ULONG64 *BatArray = NULL;
    char guidBuf[64];
    ULONG i;

    static const GUID FileParamsGuid = VHDX_META_FILE_PARAMETERS_GUID;
    static const GUID VirtDiskSizeGuid = VHDX_META_VIRTUAL_DISK_SIZE_GUID;
    static const GUID LogSectorGuid = VHDX_META_LOGICAL_SECTOR_SIZE_GUID;
    static const GUID PhysSectorGuid = VHDX_META_PHYSICAL_SECTOR_SIZE_GUID;
    static const GUID VirtDiskIdGuid = VHDX_META_VIRTUAL_DISK_ID_GUID;

    if (argc < 1)
    {
        LogError("Usage: vhdxtool info input.vhdx");
        return 1;
    }
    InputPath = argv[0];

    hFile = CreateFileA(InputPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        LogError("Failed to open '%s' (error %lu)", InputPath, GetLastError());
        return 1;
    }

    if (!GetFileSizeEx(hFile, &FileSize))
    {
        LogError("Failed to get file size (error %lu)", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    printf("=== VHDX Info: %s ===\n", InputPath);
    printf("File size: %llu bytes (%llu MB)\n\n",
           (unsigned long long)FileSize.QuadPart,
           (unsigned long long)(FileSize.QuadPart / (1024 * 1024)));

    /* --- File Identifier --- */
    if (!ReadAt(hFile, VHDX_FILE_ID_OFFSET, &FileId, sizeof(FileId)))
    {
        LogError("Failed to read file identifier");
        goto info_fail;
    }

    if (FileId.Signature != VHDX_FILE_SIGNATURE)
    {
        LogError("Invalid file signature: 0x%016llx (expected 0x%016llx)",
                 (unsigned long long)FileId.Signature,
                 (unsigned long long)VHDX_FILE_SIGNATURE);
        goto info_fail;
    }

    printf("[File Identifier]\n");
    printf("  Signature: 0x%016llx (\"vhdxfile\") OK\n",
           (unsigned long long)FileId.Signature);
    printf("  Creator: \"%ls\"\n\n", FileId.Creator);

    /* --- Headers --- */
    if (!ReadAt(hFile, VHDX_HEADER1_OFFSET, &Header1, sizeof(Header1)))
    {
        LogError("Failed to read Header 1");
        goto info_fail;
    }
    if (!ReadAt(hFile, VHDX_HEADER2_OFFSET, &Header2, sizeof(Header2)))
    {
        LogError("Failed to read Header 2");
        goto info_fail;
    }

    printf("[Headers]\n");

    /* Verify header signatures */
    {
        BOOL H1Valid = (Header1.Signature == VHDX_HEADER_SIGNATURE);
        BOOL H2Valid = (Header2.Signature == VHDX_HEADER_SIGNATURE);
        ULONG H1Crc, H2Crc, H1Stored, H2Stored;

        /* Verify CRC-32C for Header 1 */
        H1Stored = Header1.Checksum;
        Header1.Checksum = 0;
        H1Crc = VhdxCrc32c(&Header1, sizeof(Header1));
        Header1.Checksum = H1Stored;
        if (H1Crc != H1Stored) H1Valid = FALSE;

        /* Verify CRC-32C for Header 2 */
        H2Stored = Header2.Checksum;
        Header2.Checksum = 0;
        H2Crc = VhdxCrc32c(&Header2, sizeof(Header2));
        Header2.Checksum = H2Stored;
        if (H2Crc != H2Stored) H2Valid = FALSE;

        printf("  Header 1: sig=0x%08lx seq=%llu crc=%s\n",
               Header1.Signature,
               (unsigned long long)Header1.SequenceNumber,
               (H1Crc == H1Stored) ? "OK" : "MISMATCH");
        printf("  Header 2: sig=0x%08lx seq=%llu crc=%s\n",
               Header2.Signature,
               (unsigned long long)Header2.SequenceNumber,
               (H2Crc == H2Stored) ? "OK" : "MISMATCH");

        /* Pick active header */
        if (H1Valid && H2Valid)
        {
            if (Header1.SequenceNumber >= Header2.SequenceNumber)
            {
                ActiveHeader = &Header1;
                ActiveHeaderIdx = 1;
            }
            else
            {
                ActiveHeader = &Header2;
                ActiveHeaderIdx = 2;
            }
        }
        else if (H1Valid)
        {
            ActiveHeader = &Header1;
            ActiveHeaderIdx = 1;
        }
        else if (H2Valid)
        {
            ActiveHeader = &Header2;
            ActiveHeaderIdx = 2;
        }
        else
        {
            LogError("Both headers are invalid - file is corrupt");
            goto info_fail;
        }
    }

    printf("  Active header: %d (seq %llu)\n", ActiveHeaderIdx,
           (unsigned long long)ActiveHeader->SequenceNumber);
    printf("  Format version: %u\n", ActiveHeader->Version);
    printf("  Log version: %u\n", ActiveHeader->LogVersion);
    FormatGuid(&ActiveHeader->FileWriteGuid, guidBuf, sizeof(guidBuf));
    printf("  FileWriteGuid: %s\n", guidBuf);
    FormatGuid(&ActiveHeader->DataWriteGuid, guidBuf, sizeof(guidBuf));
    printf("  DataWriteGuid: %s\n", guidBuf);
    FormatGuid(&ActiveHeader->LogGuid, guidBuf, sizeof(guidBuf));
    printf("  LogGuid: %s%s\n", guidBuf,
           IsNullGuid(&ActiveHeader->LogGuid) ? " (clean)" : " (DIRTY - needs replay)");
    printf("  LogOffset: 0x%llx, LogLength: 0x%lx (%lu MB)\n\n",
           (unsigned long long)ActiveHeader->LogOffset,
           ActiveHeader->LogLength,
           ActiveHeader->LogLength / (1024 * 1024));

    /* --- Region Table --- */
    RegionBuf = (unsigned char *)malloc(VHDX_REGION_TABLE_SIZE);
    if (!RegionBuf)
    {
        LogError("Failed to allocate region table buffer");
        goto info_fail;
    }

    if (!ReadAt(hFile, VHDX_REGION_TABLE1_OFFSET, RegionBuf, VHDX_REGION_TABLE_SIZE))
    {
        LogError("Failed to read Region Table 1");
        goto info_fail;
    }

    RegHdr = (VHDX_REGION_TABLE_HEADER *)RegionBuf;
    if (RegHdr->Signature != VHDX_REGION_SIGNATURE)
    {
        LogError("Invalid region table signature: 0x%08lx", RegHdr->Signature);
        goto info_fail;
    }

    /* Verify CRC */
    {
        ULONG StoredCrc = RegHdr->Checksum;
        ULONG ComputedCrc;
        RegHdr->Checksum = 0;
        ComputedCrc = VhdxCrc32c(RegionBuf, VHDX_REGION_TABLE_SIZE);
        RegHdr->Checksum = StoredCrc;

        printf("[Region Table]\n");
        printf("  Signature: 0x%08lx (\"regi\") OK\n", RegHdr->Signature);
        printf("  Checksum: %s\n", (ComputedCrc == StoredCrc) ? "OK" : "MISMATCH");
        printf("  Entry count: %lu\n", RegHdr->EntryCount);
    }

    {
        static const GUID BatGuid = VHDX_BAT_REGION_GUID;
        static const GUID MetaGuid = VHDX_METADATA_REGION_GUID;

        for (i = 0; i < RegHdr->EntryCount && i < VHDX_MAX_REGION_ENTRIES; i++)
        {
            RegEntry = (VHDX_REGION_TABLE_ENTRY *)(RegionBuf + sizeof(VHDX_REGION_TABLE_HEADER)
                         + i * sizeof(VHDX_REGION_TABLE_ENTRY));

            FormatGuid(&RegEntry->Guid, guidBuf, sizeof(guidBuf));

            if (memcmp(&RegEntry->Guid, &BatGuid, sizeof(GUID)) == 0)
            {
                printf("  Entry %lu: BAT at 0x%llx, length 0x%lx (%lu MB), required=%lu\n",
                       i, (unsigned long long)RegEntry->FileOffset,
                       RegEntry->Length, RegEntry->Length / (1024 * 1024),
                       RegEntry->Required);
                BatOffset = RegEntry->FileOffset;
                BatLength = RegEntry->Length;
            }
            else if (memcmp(&RegEntry->Guid, &MetaGuid, sizeof(GUID)) == 0)
            {
                printf("  Entry %lu: Metadata at 0x%llx, length 0x%lx (%lu MB), required=%lu\n",
                       i, (unsigned long long)RegEntry->FileOffset,
                       RegEntry->Length, RegEntry->Length / (1024 * 1024),
                       RegEntry->Required);
                MetadataOffset = RegEntry->FileOffset;
                MetadataLength = RegEntry->Length;
            }
            else
            {
                printf("  Entry %lu: Unknown %s at 0x%llx, length 0x%lx, required=%lu\n",
                       i, guidBuf, (unsigned long long)RegEntry->FileOffset,
                       RegEntry->Length, RegEntry->Required);
            }
        }
    }
    printf("\n");

    free(RegionBuf);
    RegionBuf = NULL;

    /* --- Metadata --- */
    if (MetadataOffset == 0 || MetadataLength == 0)
    {
        LogError("No metadata region found");
        goto info_fail;
    }

    MetaBuf = (unsigned char *)malloc(MetadataLength);
    if (!MetaBuf)
    {
        LogError("Failed to allocate metadata buffer (%lu bytes)", MetadataLength);
        goto info_fail;
    }

    if (!ReadAt(hFile, MetadataOffset, MetaBuf, MetadataLength))
    {
        LogError("Failed to read metadata region");
        goto info_fail;
    }

    MetaHdr = (VHDX_METADATA_TABLE_HEADER *)MetaBuf;
    if (MetaHdr->Signature != VHDX_METADATA_SIGNATURE)
    {
        LogError("Invalid metadata signature: 0x%016llx",
                 (unsigned long long)MetaHdr->Signature);
        goto info_fail;
    }

    printf("[Metadata]\n");
    printf("  Signature: 0x%016llx (\"metadata\") OK\n",
           (unsigned long long)MetaHdr->Signature);
    printf("  Entry count: %u\n", MetaHdr->EntryCount);

    for (i = 0; i < MetaHdr->EntryCount && i < VHDX_MAX_METADATA_ENTRIES; i++)
    {
        MetaEntry = (VHDX_METADATA_TABLE_ENTRY *)(MetaBuf + sizeof(VHDX_METADATA_TABLE_HEADER)
                     + i * sizeof(VHDX_METADATA_TABLE_ENTRY));

        if (memcmp(&MetaEntry->ItemId, &FileParamsGuid, sizeof(GUID)) == 0)
        {
            VHDX_FILE_PARAMETERS *fp;
            if (MetaEntry->Offset + MetaEntry->Length <= MetadataLength)
            {
                fp = (VHDX_FILE_PARAMETERS *)(MetaBuf + MetaEntry->Offset);
                BlockSize = fp->BlockSize;
                FileParamsFlags = fp->Flags;
                FoundFileParams = TRUE;
                printf("  FileParameters: BlockSize=%lu (%lu MB), Flags=0x%lx",
                       fp->BlockSize, fp->BlockSize / (1024 * 1024), fp->Flags);
                if (fp->Flags & VHDX_FILE_PARAMS_LEAVE_BLOCKS_ALLOCATED)
                    printf(" [Fixed]");
                if (fp->Flags & VHDX_FILE_PARAMS_HAS_PARENT)
                    printf(" [HasParent]");
                printf("\n");
            }
        }
        else if (memcmp(&MetaEntry->ItemId, &VirtDiskSizeGuid, sizeof(GUID)) == 0)
        {
            if (MetaEntry->Offset + MetaEntry->Length <= MetadataLength)
            {
                VirtualDiskSize = *(ULONG64 *)(MetaBuf + MetaEntry->Offset);
                FoundVirtSize = TRUE;
                printf("  VirtualDiskSize: %llu bytes (%llu MB, %llu GB)\n",
                       (unsigned long long)VirtualDiskSize,
                       (unsigned long long)(VirtualDiskSize / (1024 * 1024)),
                       (unsigned long long)(VirtualDiskSize / (1024 * 1024 * 1024)));
            }
        }
        else if (memcmp(&MetaEntry->ItemId, &LogSectorGuid, sizeof(GUID)) == 0)
        {
            if (MetaEntry->Offset + MetaEntry->Length <= MetadataLength)
            {
                LogicalSectorSize = *(ULONG *)(MetaBuf + MetaEntry->Offset);
                printf("  LogicalSectorSize: %lu\n", LogicalSectorSize);
            }
        }
        else if (memcmp(&MetaEntry->ItemId, &PhysSectorGuid, sizeof(GUID)) == 0)
        {
            if (MetaEntry->Offset + MetaEntry->Length <= MetadataLength)
            {
                PhysicalSectorSize = *(ULONG *)(MetaBuf + MetaEntry->Offset);
                printf("  PhysicalSectorSize: %lu\n", PhysicalSectorSize);
            }
        }
        else if (memcmp(&MetaEntry->ItemId, &VirtDiskIdGuid, sizeof(GUID)) == 0)
        {
            if (MetaEntry->Offset + MetaEntry->Length <= MetadataLength)
            {
                VirtualDiskId = *(GUID *)(MetaBuf + MetaEntry->Offset);
                FormatGuid(&VirtualDiskId, guidBuf, sizeof(guidBuf));
                printf("  VirtualDiskId: %s\n", guidBuf);
            }
        }
        else
        {
            FormatGuid(&MetaEntry->ItemId, guidBuf, sizeof(guidBuf));
            printf("  Unknown metadata %s: offset=%lu, length=%lu, flags=0x%lx\n",
                   guidBuf, MetaEntry->Offset, MetaEntry->Length, MetaEntry->Flags);
        }
    }

    free(MetaBuf);
    MetaBuf = NULL;

    /* --- Disk Type Summary --- */
    printf("\n[Disk Type]\n");
    if (FileParamsFlags & VHDX_FILE_PARAMS_HAS_PARENT)
        printf("  Type: Differencing\n");
    else if (FileParamsFlags & VHDX_FILE_PARAMS_LEAVE_BLOCKS_ALLOCATED)
        printf("  Type: Fixed\n");
    else
        printf("  Type: Dynamic\n");

    /* --- BAT Statistics --- */
    if (!FoundFileParams || !FoundVirtSize)
    {
        LogError("Missing required metadata (FileParams=%s, VirtualDiskSize=%s)",
                 FoundFileParams ? "found" : "MISSING",
                 FoundVirtSize ? "found" : "MISSING");
        goto info_fail;
    }

    if (BatOffset == 0 || BatLength == 0)
    {
        LogError("No BAT region found");
        goto info_fail;
    }

    DataBlocksCount = CalcDataBlocksCount(VirtualDiskSize, BlockSize);
    ChunkRatio = CalcChunkRatio(BlockSize, LogicalSectorSize);
    TotalBatEntries = CalcTotalBatEntries(DataBlocksCount, ChunkRatio);

    printf("\n[BAT Statistics]\n");
    printf("  Data blocks: %lu\n", DataBlocksCount);
    printf("  Chunk ratio: %lu\n", ChunkRatio);
    printf("  Total BAT entries: %lu (computed)\n", TotalBatEntries);
    printf("  BAT region: offset 0x%llx, length 0x%lx\n",
           (unsigned long long)BatOffset, BatLength);

    {
        ULONG BatReadSize = TotalBatEntries * 8;
        ULONG StatNotPresent = 0, StatUndefined = 0, StatZero = 0;
        ULONG StatUnmapped = 0, StatFullyPresent = 0, StatPartiallyPresent = 0;
        ULONG StatReserved = 0, StatSbPresent = 0, StatSbNotPresent = 0;

        if (BatReadSize > BatLength)
        {
            LogError("BAT entries (%lu bytes) exceeds region length (%lu bytes)",
                     BatReadSize, BatLength);
            goto info_fail;
        }

        BatArray = (ULONG64 *)malloc(BatReadSize);
        if (!BatArray)
        {
            LogError("Failed to allocate BAT read buffer");
            goto info_fail;
        }

        if (!ReadAt(hFile, BatOffset, BatArray, BatReadSize))
        {
            LogError("Failed to read BAT");
            goto info_fail;
        }

        for (i = 0; i < TotalBatEntries; i++)
        {
            ULONG State = VHDX_BAT_STATE(BatArray[i]);

            if (IsSectorBitmapIndex(i, ChunkRatio))
            {
                if (State == VHDX_SB_BLOCK_PRESENT)
                    StatSbPresent++;
                else
                    StatSbNotPresent++;
            }
            else
            {
                switch (State)
                {
                case VHDX_PAYLOAD_BLOCK_NOT_PRESENT:        StatNotPresent++; break;
                case VHDX_PAYLOAD_BLOCK_UNDEFINED:          StatUndefined++; break;
                case VHDX_PAYLOAD_BLOCK_ZERO:               StatZero++; break;
                case VHDX_PAYLOAD_BLOCK_UNMAPPED:           StatUnmapped++; break;
                case VHDX_PAYLOAD_BLOCK_FULLY_PRESENT:      StatFullyPresent++; break;
                case VHDX_PAYLOAD_BLOCK_PARTIALLY_PRESENT:  StatPartiallyPresent++; break;
                default:                                     StatReserved++; break;
                }
            }
        }

        printf("  Payload block states:\n");
        if (StatNotPresent)     printf("    NOT_PRESENT:        %lu\n", StatNotPresent);
        if (StatUndefined)      printf("    UNDEFINED:          %lu\n", StatUndefined);
        if (StatZero)           printf("    ZERO:               %lu\n", StatZero);
        if (StatUnmapped)       printf("    UNMAPPED:           %lu\n", StatUnmapped);
        if (StatFullyPresent)   printf("    FULLY_PRESENT:      %lu\n", StatFullyPresent);
        if (StatPartiallyPresent) printf("    PARTIALLY_PRESENT:  %lu\n", StatPartiallyPresent);
        if (StatReserved)       printf("    RESERVED:           %lu\n", StatReserved);

        if (StatSbPresent || StatSbNotPresent)
        {
            printf("  Sector bitmap entries:\n");
            printf("    PRESENT:            %lu\n", StatSbPresent);
            printf("    NOT_PRESENT:        %lu\n", StatSbNotPresent);
        }

        /* Disk utilization */
        if (StatFullyPresent > 0)
        {
            ULONG64 AllocatedBytes = (ULONG64)StatFullyPresent * BlockSize;
            printf("  Allocated data: %llu MB (%lu blocks x %lu MB)\n",
                   (unsigned long long)(AllocatedBytes / (1024 * 1024)),
                   StatFullyPresent, BlockSize / (1024 * 1024));
            if (VirtualDiskSize > 0)
            {
                printf("  Utilization: %.1f%%\n",
                       (double)AllocatedBytes / (double)VirtualDiskSize * 100.0);
            }
        }
        else
        {
            printf("  Allocated data: 0 MB (empty disk)\n");
        }

        free(BatArray);
        BatArray = NULL;
    }

    printf("\n=== End of VHDX info ===\n");

    CloseHandle(hFile);
    return 0;

info_fail:
    if (RegionBuf) free(RegionBuf);
    if (MetaBuf) free(MetaBuf);
    if (BatArray) free(BatArray);
    CloseHandle(hFile);
    return 1;
}

/* ========================================================================== */
/* Usage and main                                                              */
/* ========================================================================== */

static void
PrintUsage(void)
{
    printf("ROSV VHDX Tool - Create, convert, and inspect VHDX virtual disk files\n\n");
    printf("Usage:\n");
    printf("  vhdxtool create  --type dynamic|fixed --size <N>G [--block-size <N>M] output.vhdx\n");
    printf("  vhdxtool convert input.img output.vhdx [--type dynamic|fixed] [--block-size <N>M]\n");
    printf("  vhdxtool info    input.vhdx\n");
    printf("\nExamples:\n");
    printf("  vhdxtool create --type dynamic --size 10G disk.vhdx\n");
    printf("  vhdxtool create --type fixed --size 2G --block-size 32M disk.vhdx\n");
    printf("  vhdxtool convert rootfs.img rootfs.vhdx --type dynamic\n");
    printf("  vhdxtool info disk.vhdx\n");
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    if (strcmp(argv[1], "create") == 0)
    {
        return CmdCreate(argc - 2, argv + 2);
    }
    else if (strcmp(argv[1], "convert") == 0)
    {
        return CmdConvert(argc - 2, argv + 2);
    }
    else if (strcmp(argv[1], "info") == 0)
    {
        return CmdInfo(argc - 2, argv + 2);
    }
    else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
             strcmp(argv[1], "help") == 0)
    {
        PrintUsage();
        return 0;
    }
    else
    {
        LogError("Unknown subcommand: '%s'", argv[1]);
        PrintUsage();
        return 1;
    }
}
