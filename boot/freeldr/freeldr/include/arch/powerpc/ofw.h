/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Open Firmware client interface for the little-endian loader
 */

#pragma once

/*
 * Handoff record from the big-endian stage0. Stage0 writes it in
 * little-endian byte order and enters the loader entry descriptor with
 * r3 = this record, r1 = the loader stack and MSR[LE] set. Open Firmware
 * is reached through OfwGate: the big-endian gate at BeGate calls the
 * client interface at OfEntry and returns to little-endian code.
 */
#define PPC_STAGE0_MAGIC   0x30475453 /* 'STG0' */
#define PPC_STAGE0_VERSION 1

typedef struct _PPC_STAGE0_INFO
{
    ULONG Magic;
    ULONG Version;
    ULONG OfEntry;      /* Client interface entry (BE code). */
    ULONG BeGate;       /* BE gate: r3 = args, r4 = OfEntry, r10 = LE resume. */
    ULONG Stage0Base;   /* Physical extent of stage0, including its stacks. */
    ULONG Stage0Size;
    ULONG ImageBase;    /* Physical extent of the loaded loader image. */
    ULONG ImageSize;
    ULONG InitrdBase;   /* QEMU -initrd image, 0 if none. */
    ULONG InitrdSize;
    ULONG MachineId;    /* OF root "compatible"/model hash, informational. */
    ULONG Reserved[5];
} PPC_STAGE0_INFO, *PPPC_STAGE0_INFO;

typedef ULONG OFW_HANDLE;   /* phandle or ihandle */
#define OFW_INVALID 0

extern PPPC_STAGE0_INFO OfwStage0Info;
extern OFW_HANDLE OfwStdin;
extern OFW_HANDLE OfwStdout;
extern BOOLEAN OfwAvailable;

/* LE -> BE gate implemented in ofwgate.S. Args is the client interface
 * argument array (cells already in big-endian order). */
LONG
OfwGate(
    _Inout_ PULONG Args,
    _In_ ULONG BeGate,
    _In_ ULONG OfEntry);

BOOLEAN OfwInitialize(_In_ PPPC_STAGE0_INFO Info);
LONG OfwCall(_In_z_ PCSTR Service, _In_ ULONG ArgCount, _In_ ULONG RetCount, _Inout_ PULONG Cells);
OFW_HANDLE OfwFindDevice(_In_z_ PCSTR Path);
OFW_HANDLE OfwPeer(_In_ OFW_HANDLE Phandle);
OFW_HANDLE OfwChild(_In_ OFW_HANDLE Phandle);
LONG OfwGetPropLen(_In_ OFW_HANDLE Phandle, _In_z_ PCSTR Name);
LONG OfwGetProp(_In_ OFW_HANDLE Phandle, _In_z_ PCSTR Name, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length);
BOOLEAN OfwGetPropCell(_In_ OFW_HANDLE Phandle, _In_z_ PCSTR Name, _Out_ PULONG Value);
LONG OfwPackageToPath(_In_ OFW_HANDLE Phandle, _Out_writes_(Length) PCHAR Buffer, _In_ ULONG Length);
OFW_HANDLE OfwOpen(_In_z_ PCSTR Path);
VOID OfwClose(_In_ OFW_HANDLE Ihandle);
LONG OfwRead(_In_ OFW_HANDLE Ihandle, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length);
LONG OfwWrite(_In_ OFW_HANDLE Ihandle, _In_reads_bytes_(Length) const VOID *Buffer, _In_ ULONG Length);
LONG OfwSeek(_In_ OFW_HANDLE Ihandle, _In_ ULONGLONG Position);
ULONG OfwClaim(_In_ ULONG Virt, _In_ ULONG Size, _In_ ULONG Align);
ULONG OfwMilliseconds(VOID);
LONG OfwCallMethod1(_In_z_ PCSTR Method, _In_ OFW_HANDLE Ihandle, _Out_ PULONG Result);
DECLSPEC_NORETURN VOID OfwExit(VOID);

/* Big-endian cell helpers for property buffers. */
FORCEINLINE ULONG OfwCellToHost(_In_ ULONG Cell)
{
    return _byteswap_ulong(Cell);
}

/* Machine backend (ofw*.c). */
VOID OfwConsPutChar(int Ch);
BOOLEAN OfwConsKbHit(VOID);
int OfwConsGetCh(VOID);
VOID OfwConsWriteString(_In_z_ PCSTR String);

VOID OfwVideoClearScreen(UCHAR Attr);
VIDEODISPLAYMODE OfwVideoSetDisplayMode(PCSTR DisplayMode, BOOLEAN Init);
VOID OfwVideoGetDisplaySize(PULONG Width, PULONG Height, PULONG Depth);
ULONG OfwVideoGetBufferSize(VOID);
VOID OfwVideoGetFontsFromFirmware(PULONG RomFontPointers);
VOID OfwVideoSetTextCursorPosition(UCHAR X, UCHAR Y);
VOID OfwVideoHideShowTextCursor(BOOLEAN Show);
VOID OfwVideoPutChar(int Ch, UCHAR Attr, unsigned X, unsigned Y);
VOID OfwVideoCopyOffScreenBufferToVRAM(PVOID Buffer);
BOOLEAN OfwVideoIsPaletteFixed(VOID);
VOID OfwVideoSetPaletteColor(UCHAR Color, UCHAR Red, UCHAR Green, UCHAR Blue);
VOID OfwVideoGetPaletteColor(UCHAR Color, UCHAR *Red, UCHAR *Green, UCHAR *Blue);
VOID OfwVideoSync(VOID);
VOID OfwBeep(VOID);

PFREELDR_MEMORY_DESCRIPTOR OfwMemGetMemoryMap(ULONG *MemoryMapSize);
VOID OfwGetExtendedBIOSData(PULONG ExtendedBIOSDataArea, PULONG ExtendedBIOSDataSize);

UCHAR OfwGetFloppyCount(VOID);
BOOLEAN OfwDiskReadLogicalSectors(UCHAR DriveNumber, ULONGLONG SectorNumber, ULONG SectorCount, PVOID Buffer);
BOOLEAN OfwDiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry);
ULONG OfwDiskGetCacheableBlockCount(UCHAR DriveNumber);
BOOLEAN OfwInitializeBootDevices(VOID);

TIMEINFO *OfwGetTime(VOID);
ULONG OfwGetRelativeTime(VOID);
PCONFIGURATION_COMPONENT_DATA OfwHwDetect(_In_opt_ PCSTR Options);
VOID OfwHwIdle(VOID);
VOID OfwPrepareForReactOS(VOID);

/* Machine facts gathered from the device tree for the kernel loader block. */
typedef struct _OFW_MACHINE_INFO
{
    ULONG MachineType;
    ULONG TimebaseFrequency;
    ULONG ProcessorFrequency;
    ULONG BusFrequency;
    ULONG DcacheLineSize;
    ULONG IcacheLineSize;
    ULONG DcacheSize;
    ULONG IcacheSize;
    ULONG MemorySize;
    ULONG IsaIoPhysicalBase;
    ULONG PciMemoryPhysicalBase;
    ULONG PciDmaOffset;
    ULONG ConsolePort;
    CHAR Model[64];
} OFW_MACHINE_INFO, *POFW_MACHINE_INFO;

extern OFW_MACHINE_INFO OfwMachine;
VOID OfwDetectMachine(VOID);
