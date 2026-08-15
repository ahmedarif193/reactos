/*
 * PROJECT:         ReactOS NT Library
 * FILE:            dll/ntdll/chpe.c
 * PURPOSE:         CHPE (ARM64EC) emulator integration for x64-on-ARM64
 *
 * CHPE (Compiled Hybrid Portable Executable) allows running x86_64 (AMD64)
 * binaries on an ARM64 host by loading the FEX ARM64EC emulator DLL and
 * calling its process/thread init, exception translation, and memory
 * notification hooks.
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

/* Function pointer types for the arm64ecfex.dll exports */
typedef NTSTATUS (NTAPI *PCHPE_PROCESS_INIT)(VOID);
typedef VOID     (NTAPI *PCHPE_PROCESS_TERM)(HANDLE, BOOLEAN, NTSTATUS);
typedef NTSTATUS (NTAPI *PCHPE_THREAD_INIT)(VOID);
typedef NTSTATUS (NTAPI *PCHPE_THREAD_TERM)(HANDLE, LONG);
typedef NTSTATUS (NTAPI *PCHPE_RESET_TO_CONSISTENT_STATE)(PEXCEPTION_RECORD, PCONTEXT, PVOID /* ARM64_NT_CONTEXT * */);

typedef VOID     (NTAPI *PCHPE_NOTIFY_MEMORY_ALLOC)(PVOID, SIZE_T, ULONG, ULONG, BOOLEAN, NTSTATUS);
typedef VOID     (NTAPI *PCHPE_NOTIFY_MEMORY_FREE)(PVOID, SIZE_T, ULONG, BOOLEAN, NTSTATUS);
typedef VOID     (NTAPI *PCHPE_NOTIFY_MEMORY_PROTECT)(PVOID, SIZE_T, ULONG, BOOLEAN, NTSTATUS);
typedef NTSTATUS (NTAPI *PCHPE_NOTIFY_MAP_VIEW)(PVOID, PVOID, PVOID, SIZE_T, ULONG, ULONG);
typedef VOID     (NTAPI *PCHPE_NOTIFY_UNMAP_VIEW)(PVOID, BOOLEAN, NTSTATUS);
typedef VOID     (NTAPI *PCHPE_FLUSH_ICACHE_HEAVY)(const void *, SIZE_T);
typedef VOID     (NTAPI *PCHPE_FLUSH_ICACHE)(const void *, SIZE_T);
typedef VOID     (NTAPI *PCHPE_NOTIFY_MEMORY_DIRTY)(void *, SIZE_T);
typedef VOID     (NTAPI *PCHPE_NOTIFY_READ_FILE)(HANDLE, void *, SIZE_T, BOOLEAN, NTSTATUS);
typedef BOOLEAN  (WINAPI *PCHPE_IS_PROCESSOR_FEATURE_PRESENT)(UINT);
typedef VOID     (NTAPI *PCHPE_UPDATE_PROCESSOR_INFO)(PVOID);

typedef struct _CHPE_V2_CPU_AREA_INFO
{
    BOOLEAN InSimulation;
    BOOLEAN InSyscallCallback;
    UCHAR Reserved0[6];
    ULONG64 EmulatorStackBase;
    ULONG64 EmulatorStackLimit;
    PVOID ContextAmd64;
    PULONG SuspendDoorbell;
    ULONG64 LoadingModuleModflag;
    PVOID EmulatorData[4];
    ULONG64 EmulatorDataInline;
} CHPE_V2_CPU_AREA_INFO, *PCHPE_V2_CPU_AREA_INFO;

typedef struct _IMAGE_ARM64EC_METADATA
{
    ULONG Version;
    ULONG CodeMap;
    ULONG CodeMapCount;
    ULONG CodeRangesToEntryPoints;
    ULONG RedirectionMetadata;
    ULONG DispatchCallNoRedirect;
    ULONG DispatchRet;
    ULONG DispatchCall;
    ULONG DispatchIcall;
    ULONG DispatchIcallCfg;
    ULONG AlternateEntryPoint;
    ULONG AuxiliaryIat;
    ULONG CodeRangesToEntryPointsCount;
    ULONG RedirectionMetadataCount;
    ULONG GetX64InformationFunctionPointer;
    ULONG SetX64InformationFunctionPointer;
    ULONG ExtraRfeTable;
    ULONG ExtraRfeTableSize;
    ULONG DispatchFptr;
    ULONG AuxiliaryIatCopy;
    ULONG Helper[9];
} IMAGE_ARM64EC_METADATA, *PIMAGE_ARM64EC_METADATA;

typedef struct _IMAGE_ARM64EC_REDIRECTION_ENTRY
{
    ULONG Source;
    ULONG Destination;
} IMAGE_ARM64EC_REDIRECTION_ENTRY, *PIMAGE_ARM64EC_REDIRECTION_ENTRY;

typedef struct _IMAGE_CHPE_RANGE_ENTRY
{
    union
    {
        ULONG StartOffset;
        struct
        {
            ULONG NativeCode : 1;
            ULONG AddressBits : 31;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    ULONG Length;
} IMAGE_CHPE_RANGE_ENTRY, *PIMAGE_CHPE_RANGE_ENTRY;

#define CHPE_TEB_CPU_AREA_OFFSET 0x1788
#define CHPE_CONTEXT_AMD64_SIZE  0x1000
#define CHPE_PEB_EC_CODE_BITMAP_OFFSET 0x368
#define CHPE_EC_CODE_BITMAP_SIZE (1ULL << 32)
#define CHPE_EC_CODE_BITMAP_INITIAL_COMMIT_SIZE 0x100000

/* Exported entry/dispatch trampolines (DATA exports, resolved at load time) */
typedef struct _CHPE_DISPATCH_TABLE
{
    PVOID DispatchJump;
    PVOID RetToEntryThunk;
    PVOID ExitToX64;
    PVOID BeginSimulation;
} CHPE_DISPATCH_TABLE, *PCHPE_DISPATCH_TABLE;

/* CHPE emulator state, stored in ntdll globals (per-process) */
static HMODULE ChpeEmulatorModule;
static PCHPE_PROCESS_INIT              pChpeProcessInit;
static PCHPE_PROCESS_TERM              pChpeProcessTerm;
static PCHPE_THREAD_INIT               pChpeThreadInit;
static PCHPE_THREAD_TERM               pChpeThreadTerm;
static PCHPE_RESET_TO_CONSISTENT_STATE pChpeResetToConsistentState;
static PCHPE_NOTIFY_MEMORY_ALLOC       pChpeNotifyMemoryAlloc;
static PCHPE_NOTIFY_MEMORY_FREE        pChpeNotifyMemoryFree;
static PCHPE_NOTIFY_MEMORY_PROTECT     pChpeNotifyMemoryProtect;
static PCHPE_NOTIFY_MAP_VIEW           pChpeNotifyMapViewOfSection;
static PCHPE_NOTIFY_UNMAP_VIEW         pChpeNotifyUnmapViewOfSection;
static PCHPE_FLUSH_ICACHE_HEAVY        pChpeFlushInstructionCacheHeavy;
static PCHPE_FLUSH_ICACHE              pChpeFlushInstructionCache;
static PCHPE_NOTIFY_MEMORY_DIRTY       pChpeNotifyMemoryDirty;
static PCHPE_NOTIFY_READ_FILE          pChpeNotifyReadFile;
static PCHPE_IS_PROCESSOR_FEATURE_PRESENT pChpeIsProcessorFeaturePresent;
static PCHPE_UPDATE_PROCESSOR_INFO     pChpeUpdateProcessorInfo;
static CHPE_DISPATCH_TABLE             ChpeDispatchTable;

/* Whether the CHPE emulator has been loaded for this process */
static BOOLEAN ChpeEmulatorLoaded = FALSE;
static PVOID volatile ChpeEcCodeBitmap;

extern PVOID NtDllBase;
extern IMAGE_DOS_HEADER __ImageBase;

static const UNICODE_STRING ChpeDllName = RTL_CONSTANT_STRING(L"arm64ecfex.dll");

static
PIMAGE_ARM64EC_METADATA
ChpepGetArm64EcMetadata(PVOID ImageBase);

static
PIMAGE_NT_HEADERS
ChpepGetImageNtHeader(PVOID ImageBase)
{
    MEMORY_BASIC_INFORMATION MemoryInfo;
    SIZE_T ReturnLength;
    NTSTATUS Status;

    if (!ImageBase)
        return NULL;

    Status = ZwQueryVirtualMemory(NtCurrentProcess(), ImageBase, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), &ReturnLength);
    if (!NT_SUCCESS(Status) ||
        MemoryInfo.AllocationBase != ImageBase ||
        MemoryInfo.Type != MEM_IMAGE)
    {
        return NULL;
    }

    return RtlImageNtHeader(ImageBase);
}

static
USHORT
ChpepGetImageMachine(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;

    if (!ImageBase)
        return IMAGE_FILE_MACHINE_UNKNOWN;

    NtHeader = ChpepGetImageNtHeader(ImageBase);
    if (!NtHeader)
        return IMAGE_FILE_MACHINE_UNKNOWN;

    if (NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && ChpepGetArm64EcMetadata(ImageBase))
        return IMAGE_FILE_MACHINE_ARM64EC;

    return NtHeader->FileHeader.Machine;
}

USHORT
NTAPI
ChpeGetImageMachine(PVOID ImageBase)
{
    return ChpepGetImageMachine(ImageBase);
}

static
PCHPE_V2_CPU_AREA_INFO *
ChpepGetTebCpuAreaSlot(VOID)
{
    return (PCHPE_V2_CPU_AREA_INFO *)((PBYTE)NtCurrentTeb() +
                                      CHPE_TEB_CPU_AREA_OFFSET);
}

static
PCHPE_V2_CPU_AREA_INFO
ChpepGetCurrentCpuArea(VOID)
{
    return *ChpepGetTebCpuAreaSlot();
}

static
BOOLEAN
ChpepIsCurrentThreadInitialized(VOID)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea = ChpepGetCurrentCpuArea();

    return CpuArea && CpuArea->EmulatorData[1];
}

static
PCHPE_V2_CPU_AREA_INFO
ChpepEnterCurrentThreadCallback(VOID)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea = ChpepGetCurrentCpuArea();

    if (!CpuArea || !CpuArea->EmulatorData[1] || CpuArea->InSyscallCallback)
        return NULL;

    CpuArea->InSyscallCallback = TRUE;
    return CpuArea;
}

static
VOID
ChpepLeaveCurrentThreadCallback(PCHPE_V2_CPU_AREA_INFO CpuArea)
{
    CpuArea->InSyscallCallback = FALSE;
}

static
PVOID *
ChpepGetPebEcCodeBitmapSlot(VOID)
{
    return (PVOID *)((PBYTE)NtCurrentPeb() + CHPE_PEB_EC_CODE_BITMAP_OFFSET);
}

static
ULONG_PTR
ChpepEcCodeBitmapOffset(ULONG_PTR Address)
{
    return (Address >> 15) & 0x1FFFFFFFFFFF8ULL;
}

static
NTSTATUS
ChpepEnsureProcessData(VOID)
{
    PVOID *EcCodeBitmapSlot = ChpepGetPebEcCodeBitmapSlot();
    PVOID EcCodeBitmap = NULL;
    PVOID ExistingBitmap;
    SIZE_T RegionSize = CHPE_EC_CODE_BITMAP_SIZE;
    NTSTATUS Status;

    ExistingBitmap = InterlockedCompareExchangePointer(EcCodeBitmapSlot, NULL, NULL);
    if (ExistingBitmap)
    {
        InterlockedCompareExchangePointer(&ChpeEcCodeBitmap, ExistingBitmap, NULL);
        return STATUS_SUCCESS;
    }

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &EcCodeBitmap, 0, &RegionSize, MEM_RESERVE, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        return Status;

    RegionSize = CHPE_EC_CODE_BITMAP_INITIAL_COMMIT_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &EcCodeBitmap, 0, &RegionSize, MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        SIZE_T FreeSize = 0;

        ZwFreeVirtualMemory(NtCurrentProcess(), &EcCodeBitmap, &FreeSize, MEM_RELEASE);
        return Status;
    }

    ExistingBitmap = InterlockedCompareExchangePointer(EcCodeBitmapSlot, EcCodeBitmap, NULL);
    if (ExistingBitmap)
    {
        SIZE_T FreeSize = 0;

        ZwFreeVirtualMemory(NtCurrentProcess(), &EcCodeBitmap, &FreeSize, MEM_RELEASE);
        EcCodeBitmap = ExistingBitmap;
    }

    InterlockedCompareExchangePointer(&ChpeEcCodeBitmap, EcCodeBitmap, NULL);
    return STATUS_SUCCESS;
}

static
BOOLEAN
ChpepCommitEcCodeBitmapRange(ULONG_PTR Address,
                             SIZE_T Length)
{
    PBYTE BitmapBase;
    ULONG_PTR StartOffset, EndOffset, CommitStart, CommitEnd;
    PVOID CommitBase;
    SIZE_T CommitSize;
    NTSTATUS Status;

    if (!Length)
        return TRUE;

    if (!NT_SUCCESS(ChpepEnsureProcessData()))
        return FALSE;

    if (Address + Length - 1 < Address)
        return FALSE;

    BitmapBase = *ChpepGetPebEcCodeBitmapSlot();
    if (!BitmapBase)
        return FALSE;

    StartOffset = ChpepEcCodeBitmapOffset(Address);
    EndOffset = ChpepEcCodeBitmapOffset(Address + Length - 1) + sizeof(ULONGLONG);
    if (EndOffset < StartOffset || EndOffset > CHPE_EC_CODE_BITMAP_SIZE)
        return FALSE;

    CommitStart = StartOffset & ~((ULONG_PTR)PAGE_SIZE - 1);
    CommitEnd = (EndOffset + PAGE_SIZE - 1) & ~((ULONG_PTR)PAGE_SIZE - 1);
    if (CommitEnd < CommitStart)
        return FALSE;

    CommitBase = BitmapBase + CommitStart;
    CommitSize = CommitEnd - CommitStart;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &CommitBase, 0, &CommitSize, MEM_COMMIT, PAGE_READWRITE);
    return NT_SUCCESS(Status) || Status == STATUS_ALREADY_COMMITTED;
}

static
VOID
ChpepFreeProcessData(VOID)
{
    PVOID *EcCodeBitmapSlot = ChpepGetPebEcCodeBitmapSlot();

    if (*EcCodeBitmapSlot == ChpeEcCodeBitmap)
        *EcCodeBitmapSlot = NULL;

    if (ChpeEcCodeBitmap)
    {
        PVOID BaseAddress = ChpeEcCodeBitmap;
        SIZE_T RegionSize = 0;

        ZwFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
        ChpeEcCodeBitmap = NULL;
    }
}

static
PIMAGE_ARM64EC_METADATA
ChpepGetArm64EcMetadata(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;
    PIMAGE_LOAD_CONFIG_DIRECTORY LoadConfig;
    ULONG ConfigSize, SizeOfImage;
    ULONG_PTR ImageStart, ImageEnd, Candidate;
    PIMAGE_ARM64EC_METADATA Metadata;

    if (!ImageBase)
        return NULL;

    NtHeader = ChpepGetImageNtHeader(ImageBase);
    if (!NtHeader ||
        (NtHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 &&
         NtHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_ARM64EC))
    {
        return NULL;
    }

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    if (SizeOfImage < sizeof(*Metadata))
        return NULL;

    ImageStart = (ULONG_PTR)ImageBase;
    ImageEnd = ImageStart + SizeOfImage;
    if (ImageEnd < ImageStart)
        return NULL;

    LoadConfig = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &ConfigSize);
    if (!LoadConfig ||
        ConfigSize < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, CHPEMetadataPointer))
        return NULL;

    Candidate = (ULONG_PTR)LoadConfig->CHPEMetadataPointer;
    if (Candidate < ImageStart || Candidate > ImageEnd - sizeof(*Metadata))
        return NULL;

    Metadata = (PIMAGE_ARM64EC_METADATA)Candidate;
    if (Metadata->Version != 1 ||
        !Metadata->CodeMap ||
        !Metadata->CodeMapCount ||
        Metadata->CodeMap >= SizeOfImage ||
        Metadata->CodeMapCount >
        (SizeOfImage - Metadata->CodeMap) / sizeof(IMAGE_CHPE_RANGE_ENTRY))
    {
        return NULL;
    }

    return Metadata;
}

static
BOOLEAN
ChpepSetEcCodePage(ULONG_PTR Page,
                   BOOLEAN Mark)
{
    PVOID BitmapBase;
    volatile LONGLONG *Bitmap;
    ULONG_PTR Index;
    ULONGLONG Mask;

    BitmapBase = *ChpepGetPebEcCodeBitmapSlot();
    if (!BitmapBase)
        return FALSE;

    Index = Page / 64;
    if (((Index + 1) * sizeof(ULONGLONG)) > CHPE_EC_CODE_BITMAP_SIZE)
        return FALSE;

    Bitmap = (volatile LONGLONG *)BitmapBase;
    Mask = 1ULL << (Page & 63);
    if (Mark)
        InterlockedOr64(&Bitmap[Index], (LONGLONG)Mask);
    else
        InterlockedAnd64(&Bitmap[Index], (LONGLONG)~Mask);

    return TRUE;
}

static
BOOLEAN
ChpepSetEcCodeRange(
    ULONG_PTR BaseAddress,
    SIZE_T Offset,
    SIZE_T Length,
    BOOLEAN Mark)
{
    ULONG_PTR Address, EndAddress, Page, EndPage;

    if (!Length)
        return TRUE;

    if (Offset > MAXULONG_PTR - BaseAddress)
        return FALSE;

    Address = BaseAddress + Offset;
    if (Length - 1 > MAXULONG_PTR - Address)
        return FALSE;

    EndAddress = Address + Length - 1;
    if (!ChpepCommitEcCodeBitmapRange(Address, Length))
        return FALSE;

    Page = Address >> PAGE_SHIFT;
    EndPage = EndAddress >> PAGE_SHIFT;

    for (; Page <= EndPage; ++Page)
    {
        if (!ChpepSetEcCodePage(Page, Mark))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
ChpepMarkEcCodeRange(ULONG_PTR BaseAddress,
                     SIZE_T Offset,
                     SIZE_T Length)
{
    return ChpepSetEcCodeRange(BaseAddress, Offset, Length, TRUE);
}

static
BOOLEAN
ChpepClearEcCodeRange(ULONG_PTR BaseAddress,
                      SIZE_T Offset,
                      SIZE_T Length)
{
    return ChpepSetEcCodeRange(BaseAddress, Offset, Length, FALSE);
}

BOOLEAN
NTAPI
ChpeMarkEcCodeRange(PVOID Address,
                    SIZE_T Length)
{
    if (!Address)
        return FALSE;

    return ChpepMarkEcCodeRange((ULONG_PTR)Address, 0, Length);
}

BOOLEAN
NTAPI
RtlIsEcCode(ULONG_PTR CodeAddress)
{
    PVOID BitmapBase;
    PULONGLONG Bitmap;
    ULONG_PTR Page, Index;

    BitmapBase = *ChpepGetPebEcCodeBitmapSlot();
    if (!BitmapBase)
        return FALSE;

    Page = CodeAddress >> PAGE_SHIFT;
    Index = Page / 64;
    if (((Index + 1) * sizeof(ULONGLONG)) > CHPE_EC_CODE_BITMAP_SIZE)
        return FALSE;

    if (!ChpepCommitEcCodeBitmapRange(CodeAddress, 1))
        return FALSE;

    Bitmap = (PULONGLONG)BitmapBase;
    return (Bitmap[Index] & (1ULL << (Page & 63))) != 0;
}

static
BOOLEAN
ChpepSetImageExecuteSections(PVOID ImageBase,
                             PIMAGE_NT_HEADERS NtHeader,
                             BOOLEAN Mark)
{
    PIMAGE_SECTION_HEADER Section;
    ULONG Index, StartRva, Length, SizeOfImage;

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    Section = IMAGE_FIRST_SECTION(NtHeader);
    for (Index = 0; Index < NtHeader->FileHeader.NumberOfSections; ++Index)
    {
        if (!(Section[Index].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

        StartRva = Section[Index].VirtualAddress;
        Length = Section[Index].Misc.VirtualSize;
        if (Length < Section[Index].SizeOfRawData)
            Length = Section[Index].SizeOfRawData;

        if (!Length || StartRva >= SizeOfImage)
            continue;

        if (Length > SizeOfImage - StartRva)
            Length = SizeOfImage - StartRva;

        if (!ChpepSetEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length, Mark))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
ChpepWritePointer(PVOID Address,
                  PVOID Value)
{
    PVOID ProtectBase;
    SIZE_T ProtectSize;
    ULONG OldProtect, IgnoredProtect;
    NTSTATUS Status;

    ProtectBase = Address;
    ProtectSize = sizeof(PVOID);
    Status = NtProtectVirtualMemory(NtCurrentProcess(),
                                    &ProtectBase,
                                    &ProtectSize,
                                    PAGE_READWRITE,
                                    &OldProtect);
    if (!NT_SUCCESS(Status))
        return FALSE;

    *(PVOID *)Address = Value;

    ProtectBase = Address;
    ProtectSize = sizeof(PVOID);
    NtProtectVirtualMemory(NtCurrentProcess(),
                           &ProtectBase,
                           &ProtectSize,
                           OldProtect,
                           &IgnoredProtect);

    return TRUE;
}

static
BOOLEAN
ChpepPatchArm64EcPointer(PVOID ImageBase,
                        ULONG SizeOfImage,
                        ULONG Rva,
                        PVOID Value)
{
    if (!ImageBase || !Value || !Rva || SizeOfImage < sizeof(PVOID) || Rva > SizeOfImage - sizeof(PVOID))
        return FALSE;

    return ChpepWritePointer((PBYTE)ImageBase + Rva, Value);
}

static
VOID
__attribute__((naked))
ChpepArm64EcNoopCheck(VOID)
{
    __asm__ volatile("ret");
}

static
__attribute__((noinline, used))
ULONG_PTR
ChpepResolveArm64EcCallTarget(ULONG_PTR Target)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR TargetRva, NativeRva;

    if (!NT_SUCCESS(LdrFindEntryForAddress((PVOID)Target, &LdrEntry)) ||
        Target < (ULONG_PTR)LdrEntry->DllBase)
        return Target;

    TargetRva = Target - (ULONG_PTR)LdrEntry->DllBase;
    if (!ChpeGetArm64EcRedirection(LdrEntry->DllBase, TargetRva, &NativeRva))
        return Target;

    return (ULONG_PTR)LdrEntry->DllBase + NativeRva;
}

static
VOID
__attribute__((naked))
ChpepArm64EcCheckCall(VOID)
{
    __asm__ volatile(
        "ldr x16, [x18, #0x60]\n"
        "ldr x16, [x16, #0x368]\n"
        "cbz x16, 1f\n"
        "lsr x17, x11, #15\n"
        "and x17, x17, #0x1fffffffffff8\n"
        "ldr x16, [x16, x17]\n"
        "lsr x17, x11, #12\n"
        "lsr x16, x16, x17\n"
        "tbnz x16, #0, 1f\n"
        "sub sp, sp, #0x100\n"
        "stp x0, x1, [sp, #0x00]\n"
        "stp x2, x3, [sp, #0x10]\n"
        "stp x4, x5, [sp, #0x20]\n"
        "stp x6, x7, [sp, #0x30]\n"
        "stp x8, x9, [sp, #0x40]\n"
        "stp x10, x11, [sp, #0x50]\n"
        "str x30, [sp, #0x60]\n"
        "str x15, [sp, #0x68]\n"
        "stp q0, q1, [sp, #0x70]\n"
        "stp q2, q3, [sp, #0x90]\n"
        "stp q4, q5, [sp, #0xb0]\n"
        "stp q6, q7, [sp, #0xd0]\n"
        "mov x0, x11\n"
        "bl ChpepResolveArm64EcCallTarget\n"
        "str x0, [sp, #0xf0]\n"
        "ldp q6, q7, [sp, #0xd0]\n"
        "ldp q4, q5, [sp, #0xb0]\n"
        "ldp q2, q3, [sp, #0x90]\n"
        "ldp q0, q1, [sp, #0x70]\n"
        "ldr x15, [sp, #0x68]\n"
        "ldr x30, [sp, #0x60]\n"
        "ldp x10, x11, [sp, #0x50]\n"
        "ldp x8, x9, [sp, #0x40]\n"
        "ldp x6, x7, [sp, #0x30]\n"
        "ldp x4, x5, [sp, #0x20]\n"
        "ldp x2, x3, [sp, #0x10]\n"
        "ldp x0, x1, [sp, #0x00]\n"
        "ldr x16, [sp, #0xf0]\n"
        "add sp, sp, #0x100\n"
        "cmp x16, x11\n"
        "b.ne 2f\n"
        "mov x9, x11\n"
        "mov x11, x10\n"
        "ret\n"
        "2:\n"
        "mov x11, x16\n"
        "1:\n"
        "ret\n");
}

/*
 * The MXCSR/FPCR/FPSR conversion and x64 information helpers below are
 * derived from Wine dlls/ntdll/unwind.h and signal_arm64ec.c.
 *
 * Copyright 1999, 2005, 2023 Alexandre Julliard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
static
ULONG64
ChpepMxCsrToFpCsr(ULONG MxCsr)
{
    ULONG Fpcr = 0, Fpsr = 0;

    if (MxCsr & 0x0001) Fpsr |= 0x0001;
    if (MxCsr & 0x0002) Fpsr |= 0x0080;
    if (MxCsr & 0x0004) Fpsr |= 0x0002;
    if (MxCsr & 0x0008) Fpsr |= 0x0004;
    if (MxCsr & 0x0010) Fpsr |= 0x0008;
    if (MxCsr & 0x0020) Fpsr |= 0x0010;

    if (MxCsr & 0x0040) Fpcr |= 0x00080000;
    if (!(MxCsr & 0x0080)) Fpcr |= 0x00000100;
    if (!(MxCsr & 0x0100)) Fpcr |= 0x00008000;
    if (!(MxCsr & 0x0200)) Fpcr |= 0x00000200;
    if (!(MxCsr & 0x0400)) Fpcr |= 0x00000400;
    if (!(MxCsr & 0x0800)) Fpcr |= 0x00000800;
    if (!(MxCsr & 0x1000)) Fpcr |= 0x00001000;
    if (MxCsr & 0x2000) Fpcr |= 0x00800000;
    if (MxCsr & 0x4000) Fpcr |= 0x00400000;
    if (MxCsr & 0x8000) Fpcr |= 0x01000000;

    return Fpcr | ((ULONG64)Fpsr << 32);
}

static
ULONG
ChpepFpCsrToMxCsr(ULONG Fpcr,
                  ULONG Fpsr)
{
    ULONG MxCsr = 0;

    if (Fpsr & 0x0001) MxCsr |= 0x0001;
    if (Fpsr & 0x0002) MxCsr |= 0x0004;
    if (Fpsr & 0x0004) MxCsr |= 0x0008;
    if (Fpsr & 0x0008) MxCsr |= 0x0010;
    if (Fpsr & 0x0010) MxCsr |= 0x0020;
    if (Fpsr & 0x0080) MxCsr |= 0x0002;

    if (Fpcr & 0x00080000) MxCsr |= 0x0040;
    if (!(Fpcr & 0x00000100)) MxCsr |= 0x0080;
    if (!(Fpcr & 0x00000200)) MxCsr |= 0x0200;
    if (!(Fpcr & 0x00000400)) MxCsr |= 0x0400;
    if (!(Fpcr & 0x00000800)) MxCsr |= 0x0800;
    if (!(Fpcr & 0x00001000)) MxCsr |= 0x1000;
    if (!(Fpcr & 0x00008000)) MxCsr |= 0x0100;
    if (Fpcr & 0x00400000) MxCsr |= 0x4000;
    if (Fpcr & 0x00800000) MxCsr |= 0x2000;
    if (Fpcr & 0x01000000) MxCsr |= 0x8000;

    return MxCsr;
}

static
NTSTATUS
NTAPI
ChpepGetX64Information(ULONG Type,
                       PVOID Output,
                       PVOID ExtraInformation)
{
    ULONG64 Fpcr, Fpsr;

    UNREFERENCED_PARAMETER(ExtraInformation);

    switch (Type)
    {
        case 0:
            __asm__ volatile("mrs %0, fpcr; mrs %1, fpsr" : "=r" (Fpcr), "=r" (Fpsr));
            *(PULONG)Output = ChpepFpCsrToMxCsr((ULONG)Fpcr, (ULONG)Fpsr);
            return STATUS_SUCCESS;

        case 2:
            *(PULONG)Output = 0x27F;
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static
NTSTATUS
NTAPI
ChpepSetX64Information(ULONG Type,
                       ULONG_PTR Input,
                       PVOID ExtraInformation)
{
    ULONG64 FpCsr;

    UNREFERENCED_PARAMETER(ExtraInformation);

    if (Type != 0)
        return STATUS_INVALID_PARAMETER;

    FpCsr = ChpepMxCsrToFpCsr((ULONG)Input);
    __asm__ volatile("msr fpcr, %0; msr fpsr, %1" :: "r" (FpCsr), "r" (FpCsr >> 32));
    return STATUS_SUCCESS;
}

static
VOID
ChpepPatchArm64EcDispatchHelpers(PVOID ImageBase,
                                 ULONG SizeOfImage,
                                 PIMAGE_ARM64EC_METADATA Metadata)
{
    PVOID CheckTarget = ChpeEmulatorLoaded ? (PVOID)ChpepArm64EcCheckCall : (PVOID)ChpepArm64EcNoopCheck;

    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->DispatchCall, CheckTarget);
    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->DispatchIcall, CheckTarget);
    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->DispatchIcallCfg, CheckTarget);
    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->GetX64InformationFunctionPointer, ChpepGetX64Information);
    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->SetX64InformationFunctionPointer, ChpepSetX64Information);

    if (!ChpeEmulatorLoaded)
        return;

    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->DispatchCallNoRedirect, ChpeDispatchTable.ExitToX64);
    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->DispatchRet, ChpeDispatchTable.RetToEntryThunk);
    ChpepPatchArm64EcPointer(ImageBase, SizeOfImage, Metadata->DispatchFptr, ChpeDispatchTable.DispatchJump);
}

BOOLEAN
NTAPI
ChpeRegisterArm64EcImage(PVOID ImageBase)
{
    PIMAGE_ARM64EC_METADATA Metadata;
    PIMAGE_CHPE_RANGE_ENTRY Range;
    PIMAGE_NT_HEADERS NtHeader;
    ULONG Index, StartRva, Length, SizeOfImage;

    Metadata = ChpepGetArm64EcMetadata(ImageBase);
    if (!Metadata)
        return FALSE;

    NtHeader = ChpepGetImageNtHeader(ImageBase);
    if (!NtHeader)
        return FALSE;

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    if (!NT_SUCCESS(ChpepEnsureProcessData()))
        return FALSE;

    ChpepPatchArm64EcDispatchHelpers(ImageBase, SizeOfImage, Metadata);

    Range = (PIMAGE_CHPE_RANGE_ENTRY)((PBYTE)ImageBase + Metadata->CodeMap);
    for (Index = 0; Index < Metadata->CodeMapCount; ++Index)
    {
        StartRva = Range[Index].StartOffset & ~1UL;
        Length = Range[Index].Length;

        if (!Length)
            continue;

        if (StartRva >= SizeOfImage || Length > SizeOfImage - StartRva)
            return FALSE;

        if (!ChpepSetEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length, (Range[Index].StartOffset & 1) != 0))
            return FALSE;
    }

    return TRUE;
}

static
VOID
ChpepClearImageCodeRanges(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;
    PIMAGE_ARM64EC_METADATA Metadata;
    PIMAGE_CHPE_RANGE_ENTRY Range;
    ULONG Index, StartRva, Length, SizeOfImage;

    NtHeader = ChpepGetImageNtHeader(ImageBase);
    if (!NtHeader)
        return;

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    Metadata = ChpepGetArm64EcMetadata(ImageBase);
    if (!Metadata)
    {
        ChpepSetImageExecuteSections(ImageBase, NtHeader, FALSE);
        return;
    }

    Range = (PIMAGE_CHPE_RANGE_ENTRY)((PBYTE)ImageBase + Metadata->CodeMap);
    for (Index = 0; Index < Metadata->CodeMapCount; ++Index)
    {
        StartRva = Range[Index].StartOffset & ~1UL;
        Length = Range[Index].Length;
        if (!Length || StartRva >= SizeOfImage)
            continue;

        if (Length > SizeOfImage - StartRva)
            Length = SizeOfImage - StartRva;

        ChpepClearEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length);
    }
}

BOOLEAN
NTAPI
ChpeRegisterImageCodeRanges(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;
    USHORT Machine;

    NtHeader = ChpepGetImageNtHeader(ImageBase);
    if (!NtHeader)
        return FALSE;

    Machine = ChpepGetImageMachine(ImageBase);
    if (Machine == IMAGE_FILE_MACHINE_ARM64EC)
        return ChpeRegisterArm64EcImage(ImageBase);

    if (!NT_SUCCESS(ChpepEnsureProcessData()))
        return FALSE;

    if (Machine == IMAGE_FILE_MACHINE_AMD64)
        return ChpepSetImageExecuteSections(ImageBase, NtHeader, FALSE);

    if (Machine == IMAGE_FILE_MACHINE_ARM64)
        return ChpepSetImageExecuteSections(ImageBase, NtHeader, TRUE);

    return TRUE;
}

static
VOID
ChpepRegisterNativeRuntimeImages(VOID)
{
    PPEB Peb = NtCurrentPeb();
    PLIST_ENTRY ListHead, ListEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PVOID CurrentNtDllBase = &__ImageBase;

    ChpeRegisterImageCodeRanges(CurrentNtDllBase);
    if (NtDllBase && NtDllBase != CurrentNtDllBase)
        ChpeRegisterImageCodeRanges(NtDllBase);

    if (!Peb->Ldr)
        return;

    ListHead = &Peb->Ldr->InLoadOrderModuleList;
    for (ListEntry = ListHead->Flink; ListEntry != ListHead; ListEntry = ListEntry->Flink)
    {
        LdrEntry = CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        ChpeRegisterImageCodeRanges(LdrEntry->DllBase);
    }
}

static
VOID
ChpepFreeCurrentCpuArea(VOID)
{
    PCHPE_V2_CPU_AREA_INFO *Slot = ChpepGetTebCpuAreaSlot();
    PCHPE_V2_CPU_AREA_INFO CpuArea = *Slot;

    if (!CpuArea)
        return;

    if (CpuArea->ContextAmd64)
        RtlFreeHeap(RtlGetProcessHeap(), 0, CpuArea->ContextAmd64);

    RtlFreeHeap(RtlGetProcessHeap(), 0, CpuArea);
    *Slot = NULL;
}

static
NTSTATUS
ChpepEnsureCurrentCpuArea(VOID)
{
    PCHPE_V2_CPU_AREA_INFO *Slot = ChpepGetTebCpuAreaSlot();
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (*Slot)
        return STATUS_SUCCESS;

    CpuArea = RtlAllocateHeap(RtlGetProcessHeap(),
                              HEAP_ZERO_MEMORY,
                              sizeof(*CpuArea));
    if (!CpuArea)
        return STATUS_NO_MEMORY;

    CpuArea->ContextAmd64 = RtlAllocateHeap(RtlGetProcessHeap(),
                                            HEAP_ZERO_MEMORY,
                                            CHPE_CONTEXT_AMD64_SIZE);
    if (!CpuArea->ContextAmd64)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, CpuArea);
        return STATUS_NO_MEMORY;
    }

    *Slot = CpuArea;
    return STATUS_SUCCESS;
}

static
ULONG_PTR
ChpepCallX64Routine(PVOID EntryPoint,
                    ULONG_PTR Arg0,
                    ULONG_PTR Arg1,
                    ULONG_PTR Arg2,
                    ULONG_PTR Arg3)
{
    ULONG_PTR Result;

    if (!ChpeEmulatorLoaded || !ChpeDispatchTable.ExitToX64)
        return 0;

    /* FEX uses the ARM64 callee-saved register bank for the x64 CPU state. */
    __asm__ volatile(
        "sub sp, sp, #0x100\n"
        "stp x19, x20, [sp, #0x20]\n"
        "stp x21, x22, [sp, #0x30]\n"
        "stp x23, x24, [sp, #0x40]\n"
        "stp x25, x26, [sp, #0x50]\n"
        "stp x27, x28, [sp, #0x60]\n"
        "str x29, [sp, #0x70]\n"
        "stp q8, q9, [sp, #0x80]\n"
        "stp q10, q11, [sp, #0xa0]\n"
        "stp q12, q13, [sp, #0xc0]\n"
        "stp q14, q15, [sp, #0xe0]\n"
        "mov x0, %x[arg0]\n"
        "mov x1, %x[arg1]\n"
        "mov x2, %x[arg2]\n"
        "mov x3, %x[arg3]\n"
        "mov x9, %x[target]\n"
        "mov x16, %x[dispatch]\n"
        "blr x16\n"
        "ldp q14, q15, [sp, #0xe0]\n"
        "ldp q12, q13, [sp, #0xc0]\n"
        "ldp q10, q11, [sp, #0xa0]\n"
        "ldp q8, q9, [sp, #0x80]\n"
        "ldr x29, [sp, #0x70]\n"
        "ldp x27, x28, [sp, #0x60]\n"
        "ldp x25, x26, [sp, #0x50]\n"
        "ldp x23, x24, [sp, #0x40]\n"
        "ldp x21, x22, [sp, #0x30]\n"
        "ldp x19, x20, [sp, #0x20]\n"
        "add sp, sp, #0x100\n"
        "mov %x[result], x0\n"
        : [result] "=r" (Result)
        : [arg0] "r" (Arg0),
          [arg1] "r" (Arg1),
          [arg2] "r" (Arg2),
          [arg3] "r" (Arg3),
          [target] "r" (EntryPoint),
          [dispatch] "r" (ChpeDispatchTable.ExitToX64)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
          "x8", "x9", "x10", "x11", "x12", "x13", "x14",
          "x15", "x16", "x17", "x30", "cc", "memory");

    return Result;
}

static
NTSTATUS
ChpepGetNativeProcedureAddress(PVOID Base,
                               PANSI_STRING Name,
                               PVOID *Procedure)
{
    NTSTATUS Status;
    ULONG_PTR ExportRva, NativeRva;

    Status = LdrGetProcedureAddress(Base, Name, 0, Procedure);
    if (!NT_SUCCESS(Status))
        return Status;

    ExportRva = (ULONG_PTR)*Procedure - (ULONG_PTR)Base;
    if (!ChpeGetArm64EcRedirection(Base, ExportRva, &NativeRva))
    {
        if (RtlIsEcCode((ULONG_PTR)*Procedure))
            return STATUS_SUCCESS;

        return STATUS_INVALID_IMAGE_FORMAT;
    }

    *Procedure = (PBYTE)Base + NativeRva;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ChpepLoadEmulator(VOID)
{
    ANSI_STRING ProcInitName = RTL_CONSTANT_STRING("ProcessInit");
    ANSI_STRING ProcTermName = RTL_CONSTANT_STRING("ProcessTerm");
    ANSI_STRING ThreadInitName = RTL_CONSTANT_STRING("ThreadInit");
    ANSI_STRING ThreadTermName = RTL_CONSTANT_STRING("ThreadTerm");
    ANSI_STRING ResetName = RTL_CONSTANT_STRING("ResetToConsistentState");
    ANSI_STRING NotifyAllocName = RTL_CONSTANT_STRING("NotifyMemoryAlloc");
    ANSI_STRING NotifyFreeName = RTL_CONSTANT_STRING("NotifyMemoryFree");
    ANSI_STRING NotifyProtectName = RTL_CONSTANT_STRING("NotifyMemoryProtect");
    ANSI_STRING NotifyMapName = RTL_CONSTANT_STRING("NotifyMapViewOfSection");
    ANSI_STRING NotifyUnmapName = RTL_CONSTANT_STRING("NotifyUnmapViewOfSection");
    ANSI_STRING FlushHeavyName = RTL_CONSTANT_STRING("FlushInstructionCacheHeavy");
    ANSI_STRING FlushName = RTL_CONSTANT_STRING("BTCpu64FlushInstructionCache");
    ANSI_STRING DirtyName = RTL_CONSTANT_STRING("BTCpu64NotifyMemoryDirty");
    ANSI_STRING ReadFileName = RTL_CONSTANT_STRING("BTCpu64NotifyReadFile");
    ANSI_STRING IsFeatureName = RTL_CONSTANT_STRING("BTCpu64IsProcessorFeaturePresent");
    ANSI_STRING UpdateProcInfoName = RTL_CONSTANT_STRING("UpdateProcessorInformation");
    ANSI_STRING DispatchJumpName = RTL_CONSTANT_STRING("DispatchJump");
    ANSI_STRING RetToEntryName = RTL_CONSTANT_STRING("RetToEntryThunk");
    ANSI_STRING ExitToX64Name = RTL_CONSTANT_STRING("ExitToX64");
    ANSI_STRING BeginSimName = RTL_CONSTANT_STRING("BeginSimulation");
    NTSTATUS Status;
    UNICODE_STRING DllName;
    PVOID Base;

    RtlInitUnicodeString(&DllName, ChpeDllName.Buffer);

    Status = LdrLoadDll(NULL, NULL, &DllName, &Base);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CHPE: Failed to load %wZ, Status = 0x%08lx\n", &DllName, Status);
        return Status;
    }

    ChpeEmulatorModule = Base;

#define CHPE_GET_PROC(name, field) \
    Status = ChpepGetNativeProcedureAddress(Base, &name##Name, (PVOID*)&field); \
    if (!NT_SUCCESS(Status)) { \
        DPRINT1("CHPE: Failed to resolve native ARM64EC entry for %Z, Status = 0x%08lx\n", \
                &name##Name, Status); \
        return Status; \
    }

    CHPE_GET_PROC(ProcInit, pChpeProcessInit);
    CHPE_GET_PROC(ProcTerm, pChpeProcessTerm);
    CHPE_GET_PROC(ThreadInit, pChpeThreadInit);
    CHPE_GET_PROC(ThreadTerm, pChpeThreadTerm);
    CHPE_GET_PROC(Reset, pChpeResetToConsistentState);
    CHPE_GET_PROC(NotifyAlloc, pChpeNotifyMemoryAlloc);
    CHPE_GET_PROC(NotifyFree, pChpeNotifyMemoryFree);
    CHPE_GET_PROC(NotifyProtect, pChpeNotifyMemoryProtect);
    CHPE_GET_PROC(NotifyMap, pChpeNotifyMapViewOfSection);
    CHPE_GET_PROC(NotifyUnmap, pChpeNotifyUnmapViewOfSection);
    CHPE_GET_PROC(FlushHeavy, pChpeFlushInstructionCacheHeavy);
    CHPE_GET_PROC(Flush, pChpeFlushInstructionCache);
    CHPE_GET_PROC(Dirty, pChpeNotifyMemoryDirty);
    CHPE_GET_PROC(ReadFile, pChpeNotifyReadFile);
    CHPE_GET_PROC(IsFeature, pChpeIsProcessorFeaturePresent);
    CHPE_GET_PROC(UpdateProcInfo, pChpeUpdateProcessorInfo);

    /* Resolve DATA exports (trampoline addresses) */
    Status = LdrGetProcedureAddress(Base, &DispatchJumpName, 0, (PVOID*)&ChpeDispatchTable.DispatchJump);
    if (!NT_SUCCESS(Status)) return Status;
    Status = LdrGetProcedureAddress(Base, &RetToEntryName, 0, (PVOID*)&ChpeDispatchTable.RetToEntryThunk);
    if (!NT_SUCCESS(Status)) return Status;
    Status = LdrGetProcedureAddress(Base, &ExitToX64Name, 0, (PVOID*)&ChpeDispatchTable.ExitToX64);
    if (!NT_SUCCESS(Status)) return Status;
    Status = LdrGetProcedureAddress(Base, &BeginSimName, 0, (PVOID*)&ChpeDispatchTable.BeginSimulation);
    if (!NT_SUCCESS(Status)) return Status;

#undef CHPE_GET_PROC

    ChpeEmulatorLoaded = TRUE;
    return STATUS_SUCCESS;
}

/*
 * Called by LdrpInitializeProcess for the first thread of a CHPE process.
 * Returns STATUS_SUCCESS on success, or an error if the emulator could
 * not be loaded or initialized.
 */
NTSTATUS
NTAPI
ChpeInitializeProcess(VOID)
{
    NTSTATUS Status;

    if (ChpeEmulatorLoaded)
        return STATUS_SUCCESS;

    Status = ChpepLoadEmulator();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ChpepEnsureProcessData();
    if (!NT_SUCCESS(Status))
        return Status;

    ChpepRegisterNativeRuntimeImages();

    Status = pChpeProcessInit();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[CHPE] ntdll: ProcessInit failed, Status = 0x%08lx\n", Status);
        return Status;
    }

    Status = ChpeInitializeThread();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[CHPE] ntdll: initial ThreadInit failed, Status = 0x%08lx\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

/*
 * Called by LdrpInitializeThread for each thread in a CHPE process.
 */
NTSTATUS
NTAPI
ChpeInitializeThread(VOID)
{
    NTSTATUS Status;

    if (!ChpeEmulatorLoaded)
        return STATUS_UNSUCCESSFUL;

    if (ChpepIsCurrentThreadInitialized())
        return STATUS_SUCCESS;

    Status = ChpepEnsureCurrentCpuArea();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = pChpeThreadInit();
    if (!NT_SUCCESS(Status))
        ChpepFreeCurrentCpuArea();

    return Status;
}

/*
 * Called when a CHPE thread terminates.
 */
VOID
NTAPI
ChpeCleanupThread(HANDLE ThreadHandle, LONG ExitCode)
{
    if (!ChpeEmulatorLoaded)
        return;

    if (!ChpepIsCurrentThreadInitialized())
        return;

    pChpeThreadTerm(ThreadHandle, ExitCode);
    ChpepFreeCurrentCpuArea();
}

/*
 * Called during process teardown.
 */
VOID
NTAPI
ChpeCleanupProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus)
{
    if (!ChpeEmulatorLoaded)
        return;

    pChpeProcessTerm(ProcessHandle, TRUE, ExitStatus);
    ChpepFreeProcessData();
    ChpeEmulatorLoaded = FALSE;
}

/*
 * Determine if the current process is a CHPE (x64-on-ARM64) process by
 * checking the PE header machine type of the main executable.
 *
 * Returns TRUE if the process image is IMAGE_FILE_MACHINE_AMD64
 * running on an ARM64 host.
 */
BOOLEAN
NTAPI
ChpeIsChpeProcess(VOID)
{
    PPEB Peb = NtCurrentPeb();
    PIMAGE_NT_HEADERS NtHeader;

    if (Peb == NULL || Peb->ImageBaseAddress == NULL)
        return FALSE;

    NtHeader = ChpepGetImageNtHeader(Peb->ImageBaseAddress);
    if (NtHeader == NULL)
        return FALSE;

    return NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 ||
           NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_ARM64EC;
}

/*
 * Check whether the CHPE emulator has been loaded and ProcessInit called.
 */
BOOLEAN
NTAPI
ChpeIsEmulatorReady(VOID)
{
    return ChpeEmulatorLoaded;
}

/*
 * Forward an ARM64 exception to the CHPE emulator for translation to x64.
 * Called by KiUserExceptionDispatcher before RtlDispatchException.
 *
 * If the emulator handles the exception internally, this call continues
 * execution through the native context and does not return.  If it returns,
 * dispatch the exception normally through the ARM64 SEH chain.
 */
BOOLEAN
NTAPI
ChpeDispatchException(PEXCEPTION_RECORD ExceptionRecord,
                      PCONTEXT Context)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (!ChpeEmulatorLoaded || !pChpeResetToConsistentState)
        return FALSE;

    CpuArea = ChpepGetCurrentCpuArea();
    if (!CpuArea || !CpuArea->EmulatorData[1] || !CpuArea->ContextAmd64)
        return FALSE;

    pChpeResetToConsistentState(ExceptionRecord, CpuArea->ContextAmd64, Context);
    return FALSE;
}

BOOLEAN
NTAPI
ChpeShouldEmulateImage(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;

    if (!ImageBase)
        return FALSE;

    NtHeader = ChpepGetImageNtHeader(ImageBase);
    return (NtHeader &&
            NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
}

BOOLEAN
NTAPI
ChpeGetArm64EcRedirection(PVOID ImageBase,
                          ULONG_PTR SourceRva,
                          PULONG_PTR DestinationRva)
{
    PIMAGE_NT_HEADERS NtHeader;
    ULONG Index;
    ULONG SizeOfImage;
    PIMAGE_ARM64EC_METADATA Metadata;
    PIMAGE_ARM64EC_REDIRECTION_ENTRY Redirection;

    if (!ImageBase || !DestinationRva || SourceRva > MAXULONG)
        return FALSE;

    NtHeader = ChpepGetImageNtHeader(ImageBase);
    if (!NtHeader)
        return FALSE;

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    Metadata = ChpepGetArm64EcMetadata(ImageBase);
    if (!Metadata ||
        !Metadata->RedirectionMetadata ||
        !Metadata->RedirectionMetadataCount ||
        Metadata->RedirectionMetadata >= SizeOfImage ||
        Metadata->RedirectionMetadataCount >
        (SizeOfImage - Metadata->RedirectionMetadata) / sizeof(*Redirection))
    {
        return FALSE;
    }

    Redirection = (PIMAGE_ARM64EC_REDIRECTION_ENTRY)
        ((PBYTE)ImageBase + Metadata->RedirectionMetadata);
    for (Index = 0; Index < Metadata->RedirectionMetadataCount; Index++)
    {
        if (Redirection[Index].Source == (ULONG)SourceRva)
        {
            if (Redirection[Index].Destination >= SizeOfImage)
                return FALSE;

            *DestinationRva = Redirection[Index].Destination;
            return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN
NTAPI
ChpeCallX64DllMain(PVOID EntryPoint,
                   PVOID BaseAddress,
                   ULONG Reason,
                   PVOID Context)
{
    NTSTATUS Status;

    Status = ChpeInitializeThread();
    if (!NT_SUCCESS(Status))
        return FALSE;

    return (BOOLEAN)ChpepCallX64Routine(EntryPoint,
                                        (ULONG_PTR)BaseAddress,
                                        Reason,
                                        (ULONG_PTR)Context,
                                        0);
}

VOID
NTAPI
ChpeRtlUserThreadStart(PVOID StartAddress, PVOID Parameter)
{
    NTSTATUS InitStatus;
    ULONG_PTR Status;

    if (ChpeIsChpeProcess() && ChpeIsEmulatorReady())
    {
        InitStatus = ChpeInitializeThread();
        if (!NT_SUCCESS(InitStatus))
        {
            RtlExitUserThread(InitStatus);
        }

        Status = ChpepCallX64Routine(StartAddress,
                                     (ULONG_PTR)Parameter,
                                     0,
                                     0,
                                     0);
    }
    else
    {
        Status = ((ULONG_PTR (NTAPI *)(PVOID))StartAddress)(Parameter);
    }

    RtlExitUserThread((NTSTATUS)Status);
}

NTSTATUS
WINAPI
RtlWow64GetCurrentCpuArea(USHORT *Machine, void **Context, void **CpuArea)
{
    PCHPE_V2_CPU_AREA_INFO Area = ChpepGetCurrentCpuArea();

    if (!Area)
        return STATUS_NOT_SUPPORTED;

    if (Machine)
        *Machine = IMAGE_FILE_MACHINE_AMD64;

    if (Context)
        *Context = Area->ContextAmd64;

    if (CpuArea)
        *CpuArea = Area;

    return STATUS_SUCCESS;
}

/*
 * Notify the CHPE emulator of a memory allocation.
 */
VOID
NTAPI
ChpeNotifyMemoryAlloc(PVOID Address, SIZE_T Size, ULONG Type,
                      ULONG Prot, BOOLEAN After, NTSTATUS Status)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (After && NT_SUCCESS(Status) && Address && Size &&
        (Type & MEM_RESERVE) && ChpeEcCodeBitmap)
        ChpepClearEcCodeRange((ULONG_PTR)Address, 0, Size);

    if (!ChpeEmulatorLoaded || !pChpeNotifyMemoryAlloc)
        return;

    CpuArea = ChpepEnterCurrentThreadCallback();
    if (!CpuArea)
        return;

    pChpeNotifyMemoryAlloc(Address, Size, Type, Prot, After, Status);
    ChpepLeaveCurrentThreadCallback(CpuArea);
}

/*
 * Notify the CHPE emulator of a memory free.
 */
VOID
NTAPI
ChpeNotifyMemoryFree(PVOID Address, SIZE_T Size, ULONG FreeType,
                     BOOLEAN After, NTSTATUS Status)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (!ChpeEmulatorLoaded || !pChpeNotifyMemoryFree)
        return;

    CpuArea = ChpepEnterCurrentThreadCallback();
    if (!CpuArea)
        return;

    pChpeNotifyMemoryFree(Address, Size, FreeType, After, Status);
    ChpepLeaveCurrentThreadCallback(CpuArea);
}

/*
 * Notify the CHPE emulator of a memory protection change.
 */
VOID
NTAPI
ChpeNotifyMemoryProtect(PVOID Address, SIZE_T Size, ULONG NewProt,
                        BOOLEAN After, NTSTATUS Status)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (!ChpeEmulatorLoaded || !pChpeNotifyMemoryProtect)
        return;

    CpuArea = ChpepEnterCurrentThreadCallback();
    if (!CpuArea)
        return;

    pChpeNotifyMemoryProtect(Address, Size, NewProt, After, Status);
    ChpepLeaveCurrentThreadCallback(CpuArea);
}

/*
 * Notify the CHPE emulator of a section mapping.
 */
NTSTATUS
NTAPI
ChpeNotifyMapViewOfSection(PVOID Unk1, PVOID Address, PVOID Unk2,
                           SIZE_T Size, ULONG AllocType, ULONG Prot)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea;
    NTSTATUS Status;

    if (Address && Size && ChpeEcCodeBitmap)
        ChpepClearEcCodeRange((ULONG_PTR)Address, 0, Size);

    if (!ChpeEmulatorLoaded || !pChpeNotifyMapViewOfSection)
        return STATUS_SUCCESS;

    /* FEX treats this callback as an image-map notification. NtMapViewOfSection
     * also maps data sections, so do not pass non-image views to FEX. */
    if (!ChpepGetImageNtHeader(Address))
        return STATUS_SUCCESS;

    if (!ChpeRegisterImageCodeRanges(Address))
        return STATUS_INVALID_IMAGE_FORMAT;

    CpuArea = ChpepEnterCurrentThreadCallback();
    if (!CpuArea)
        return STATUS_SUCCESS;

    Status = pChpeNotifyMapViewOfSection(Unk1, Address, Unk2, Size, AllocType, Prot);
    ChpepLeaveCurrentThreadCallback(CpuArea);
    return Status;
}

/*
 * Notify the CHPE emulator of a section unmapping.
 */
VOID
NTAPI
ChpeNotifyUnmapViewOfSection(PVOID Address, BOOLEAN After, NTSTATUS Status)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (!After && Address && ChpeEcCodeBitmap)
        ChpepClearImageCodeRanges(Address);
    else if (After && !NT_SUCCESS(Status) && Address && ChpeEcCodeBitmap)
        ChpeRegisterImageCodeRanges(Address);

    if (!ChpeEmulatorLoaded || !pChpeNotifyUnmapViewOfSection)
        return;

    CpuArea = ChpepEnterCurrentThreadCallback();
    if (!CpuArea)
        return;

    pChpeNotifyUnmapViewOfSection(Address, After, Status);
    ChpepLeaveCurrentThreadCallback(CpuArea);
}

/*
 * Flush the instruction cache for a range.
 */
VOID
NTAPI
ChpeFlushInstructionCache(const void *Address, SIZE_T Size)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (!ChpeEmulatorLoaded || !pChpeFlushInstructionCache)
        return;

    CpuArea = ChpepEnterCurrentThreadCallback();
    if (!CpuArea)
        return;

    pChpeFlushInstructionCache(Address, Size);
    ChpepLeaveCurrentThreadCallback(CpuArea);
}

/*
 * ARM64EC emulator cross-process work processing.
 * Stub: CHPEV2_PROCESS_INFO work list is not yet wired.
 */
VOID
WINAPI
ProcessPendingCrossProcessEmulatorWork(VOID)
{
    /* Empty stub for now - cross-process work items
     * (memory alloc/free/protect notifications) are handled
     * inline by the memory notification hooks in chpewrap.c */
}

/*
 * ARM64EC cross-process work list helpers (stubs).
 */
void *
WINAPI
RtlWow64PopAllCrossProcessWorkFromWorkList(void *list, BOOLEAN *flush)
{
    if (flush) *flush = FALSE;
    return NULL;
}

BOOLEAN
WINAPI
RtlWow64PushCrossProcessWorkOntoFreeList(void *list, void *entry)
{
    return TRUE;
}

#endif /* _M_ARM64 */
