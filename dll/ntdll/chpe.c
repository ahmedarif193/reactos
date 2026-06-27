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

#if defined(_M_ARM64) || defined(_M_ARM64EC)

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
#define CHPE_CONTEXT_AMD64_FULL (CONTEXT_AMD64 | 0x1L | 0x2L | 0x8L)
#define CHPE_CONTEXT_ARM64_FULL (0x00400000L | 0x1L | 0x2L | 0x4L)
#define CHPE_CONTEXT_ARM64_X18  (0x00400000L | 0x10L)

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
static PVOID ChpeEcCodeBitmap;

extern PVOID NtDllBase;
extern IMAGE_DOS_HEADER __ImageBase;

static const UNICODE_STRING ChpeDllName = RTL_CONSTANT_STRING(L"arm64ecfex.dll");

static const UNICODE_STRING ChpeArm64EcRedirectImports[] =
{
    RTL_CONSTANT_STRING(L"advapi32.dll"),
    RTL_CONSTANT_STRING(L"comctl32.dll"),
    RTL_CONSTANT_STRING(L"comdlg32.dll"),
    RTL_CONSTANT_STRING(L"gdi32.dll"),
    RTL_CONSTANT_STRING(L"kernel32.dll"),
    RTL_CONSTANT_STRING(L"libpng.dll"),
    RTL_CONSTANT_STRING(L"msvcrt.dll"),
    RTL_CONSTANT_STRING(L"shell32.dll"),
    RTL_CONSTANT_STRING(L"ucrtbase.dll"),
    RTL_CONSTANT_STRING(L"user32.dll"),
    RTL_CONSTANT_STRING(L"usp10.dll"),
};

static
PIMAGE_ARM64EC_METADATA
ChpepGetArm64EcMetadata(PVOID ImageBase);

static
VOID
ChpepDumpDispatchSlotsForAddress(PCSTR Label, PVOID Address);

static
USHORT
ChpepGetImageMachine(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;
    USHORT Machine;

    if (!ImageBase)
        return IMAGE_FILE_MACHINE_UNKNOWN;

    NtHeader = RtlImageNtHeader(ImageBase);
    if (!NtHeader)
        return IMAGE_FILE_MACHINE_UNKNOWN;

    Machine = NtHeader->FileHeader.Machine;
    if (Machine == IMAGE_FILE_MACHINE_AMD64 &&
        ChpepGetArm64EcMetadata(ImageBase))
    {
        return IMAGE_FILE_MACHINE_ARM64EC;
    }

    return Machine;
}

USHORT
NTAPI
ChpeGetImageMachine(PVOID ImageBase)
{
    return ChpepGetImageMachine(ImageBase);
}

static
BOOLEAN
ChpepIsX64CallableImageMachine(USHORT Machine)
{
    return (Machine == IMAGE_FILE_MACHINE_AMD64 ||
            Machine == IMAGE_FILE_MACHINE_ARM64EC);
}

static
ULONG
ChpepCpsrToEFlags(ULONG Cpsr)
{
    ULONG EFlags = 0x202;

    if (Cpsr & (1U << 21)) EFlags |= 0x100;  /* TF */
    if (Cpsr & (1U << 28)) EFlags |= 0x800;  /* OF */
    if (Cpsr & (1U << 29)) EFlags |= 0x001;  /* CF */
    if (Cpsr & (1U << 30)) EFlags |= 0x040;  /* ZF */
    if (Cpsr & (1U << 31)) EFlags |= 0x080;  /* SF */

    return EFlags;
}

static
ULONG
ChpepEFlagsToCpsr(ULONG EFlags)
{
    ULONG Cpsr = 0;

    if (EFlags & 0x100) Cpsr |= (1U << 21);  /* TF -> SS */
    if (EFlags & 0x800) Cpsr |= (1U << 28);  /* OF */
    if (EFlags & 0x001) Cpsr |= (1U << 29);  /* CF */
    if (EFlags & 0x040) Cpsr |= (1U << 30);  /* ZF */
    if (EFlags & 0x080) Cpsr |= (1U << 31);  /* SF */

    return Cpsr;
}

VOID
NTAPI
ChpeArm64ContextToArm64Ec(
    _Out_ PARM64EC_NT_CONTEXT Arm64EcContext,
    _In_ PARM64_NT_CONTEXT Arm64Context)
{
    RtlZeroMemory(Arm64EcContext, sizeof(*Arm64EcContext));

    Arm64EcContext->ContextFlags = CHPE_CONTEXT_AMD64_FULL;
    Arm64EcContext->AMD64_MxCsr_copy = Arm64Context->Fpcr;
    Arm64EcContext->AMD64_EFlags = ChpepCpsrToEFlags(Arm64Context->Cpsr);
    Arm64EcContext->AMD64_MxCsr = Arm64Context->Fpcr;
    Arm64EcContext->AMD64_MxCsr_Mask = Arm64Context->Fpsr;

    Arm64EcContext->X8 = Arm64Context->X8;
    Arm64EcContext->X0 = Arm64Context->X0;
    Arm64EcContext->X1 = Arm64Context->X1;
    Arm64EcContext->X27 = Arm64Context->X27;
    Arm64EcContext->Sp = Arm64Context->Sp;
    Arm64EcContext->Fp = Arm64Context->Fp;
    Arm64EcContext->X25 = Arm64Context->X25;
    Arm64EcContext->X26 = Arm64Context->X26;
    Arm64EcContext->X2 = Arm64Context->X2;
    Arm64EcContext->X3 = Arm64Context->X3;
    Arm64EcContext->X4 = Arm64Context->X4;
    Arm64EcContext->X5 = Arm64Context->X5;
    Arm64EcContext->X19 = Arm64Context->X19;
    Arm64EcContext->X20 = Arm64Context->X20;
    Arm64EcContext->X21 = Arm64Context->X21;
    Arm64EcContext->X22 = Arm64Context->X22;
    Arm64EcContext->Pc = Arm64Context->Pc;
    Arm64EcContext->Lr = Arm64Context->Lr;
    Arm64EcContext->X6 = Arm64Context->X6;
    Arm64EcContext->X7 = Arm64Context->X7;
    Arm64EcContext->X9 = Arm64Context->X9;
    Arm64EcContext->X10 = Arm64Context->X10;
    Arm64EcContext->X11 = Arm64Context->X11;
    Arm64EcContext->X12 = Arm64Context->X12;
    Arm64EcContext->X15 = Arm64Context->X15;
    Arm64EcContext->X16_0 = (USHORT)Arm64Context->X16;
    Arm64EcContext->X16_1 = (USHORT)(Arm64Context->X16 >> 16);
    Arm64EcContext->X16_2 = (USHORT)(Arm64Context->X16 >> 32);
    Arm64EcContext->X16_3 = (USHORT)(Arm64Context->X16 >> 48);
    Arm64EcContext->X17_0 = (USHORT)Arm64Context->X17;
    Arm64EcContext->X17_1 = (USHORT)(Arm64Context->X17 >> 16);
    Arm64EcContext->X17_2 = (USHORT)(Arm64Context->X17 >> 32);
    Arm64EcContext->X17_3 = (USHORT)(Arm64Context->X17 >> 48);
    RtlCopyMemory(Arm64EcContext->V, Arm64Context->V, sizeof(Arm64EcContext->V));
}

VOID
NTAPI
ChpeArm64EcContextToArm64(
    _Inout_ PARM64_NT_CONTEXT Arm64Context,
    _In_ PARM64EC_NT_CONTEXT Arm64EcContext)
{
    Arm64Context->ContextFlags = CHPE_CONTEXT_ARM64_FULL | CHPE_CONTEXT_ARM64_X18;
    Arm64Context->Cpsr = ChpepEFlagsToCpsr(Arm64EcContext->AMD64_EFlags);

    Arm64Context->X8 = Arm64EcContext->X8;
    Arm64Context->X0 = Arm64EcContext->X0;
    Arm64Context->X1 = Arm64EcContext->X1;
    Arm64Context->X27 = Arm64EcContext->X27;
    Arm64Context->Sp = Arm64EcContext->Sp;
    Arm64Context->Fp = Arm64EcContext->Fp;
    Arm64Context->X25 = Arm64EcContext->X25;
    Arm64Context->X26 = Arm64EcContext->X26;
    Arm64Context->X2 = Arm64EcContext->X2;
    Arm64Context->X3 = Arm64EcContext->X3;
    Arm64Context->X4 = Arm64EcContext->X4;
    Arm64Context->X5 = Arm64EcContext->X5;
    Arm64Context->X19 = Arm64EcContext->X19;
    Arm64Context->X20 = Arm64EcContext->X20;
    Arm64Context->X21 = Arm64EcContext->X21;
    Arm64Context->X22 = Arm64EcContext->X22;
    Arm64Context->Pc = Arm64EcContext->Pc;
    Arm64Context->Fpcr = Arm64EcContext->AMD64_MxCsr;
    Arm64Context->Fpsr = Arm64EcContext->AMD64_MxCsr_Mask;
    Arm64Context->Lr = Arm64EcContext->Lr;
    Arm64Context->X6 = Arm64EcContext->X6;
    Arm64Context->X7 = Arm64EcContext->X7;
    Arm64Context->X9 = Arm64EcContext->X9;
    Arm64Context->X10 = Arm64EcContext->X10;
    Arm64Context->X11 = Arm64EcContext->X11;
    Arm64Context->X12 = Arm64EcContext->X12;
    Arm64Context->X15 = Arm64EcContext->X15;
    Arm64Context->X16 = ((ULONG64)Arm64EcContext->X16_0) |
                        ((ULONG64)Arm64EcContext->X16_1 << 16) |
                        ((ULONG64)Arm64EcContext->X16_2 << 32) |
                        ((ULONG64)Arm64EcContext->X16_3 << 48);
    Arm64Context->X17 = ((ULONG64)Arm64EcContext->X17_0) |
                        ((ULONG64)Arm64EcContext->X17_1 << 16) |
                        ((ULONG64)Arm64EcContext->X17_2 << 32) |
                        ((ULONG64)Arm64EcContext->X17_3 << 48);
    RtlCopyMemory(Arm64Context->V, Arm64EcContext->V, sizeof(Arm64EcContext->V));
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
    SIZE_T RegionSize = CHPE_EC_CODE_BITMAP_SIZE;
    NTSTATUS Status;

    if (ChpeEcCodeBitmap)
    {
        *EcCodeBitmapSlot = ChpeEcCodeBitmap;
        return STATUS_SUCCESS;
    }

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &EcCodeBitmap,
                                     0,
                                     &RegionSize,
                                     MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        return Status;

    RegionSize = CHPE_EC_CODE_BITMAP_INITIAL_COMMIT_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &EcCodeBitmap,
                                     0,
                                     &RegionSize,
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        SIZE_T FreeSize = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(),
                            &EcCodeBitmap,
                            &FreeSize,
                            MEM_RELEASE);
        return Status;
    }

    ChpeEcCodeBitmap = EcCodeBitmap;
    *EcCodeBitmapSlot = ChpeEcCodeBitmap;
    return STATUS_SUCCESS;
}

static
BOOLEAN
ChpepCommitEcCodeBitmapRange(
    ULONG_PTR Address,
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
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &CommitBase,
                                     0,
                                     &CommitSize,
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
    return (NT_SUCCESS(Status) || Status == STATUS_ALREADY_COMMITTED);
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

        ZwFreeVirtualMemory(NtCurrentProcess(),
                            &BaseAddress,
                            &RegionSize,
                            MEM_RELEASE);
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

    NtHeader = RtlImageNtHeader(ImageBase);
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

    LoadConfig = RtlImageDirectoryEntryToData(ImageBase,
                                              TRUE,
                                              IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG,
                                              &ConfigSize);
    if (!LoadConfig ||
        ConfigSize < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY,
                                              CHPEMetadataPointer))
        return NULL;

    Candidate = (ULONG_PTR)LoadConfig->CHPEMetadataPointer;
    if (Candidate < ImageStart ||
        Candidate > ImageEnd - sizeof(*Metadata))
    {
        return NULL;
    }

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
ChpepIsPureAmd64Image(PVOID ImageBase)
{
    /*
     * ARM64EC modules are native-side participants in a CHPE process. Only
     * pure AMD64 guest images must be prevented from binding to host ARM64 DLLs.
     */
    return (ChpepGetImageMachine(ImageBase) == IMAGE_FILE_MACHINE_AMD64 &&
            ChpepGetArm64EcMetadata(ImageBase) == NULL);
}

static
BOOLEAN
ChpepNeedsChpeImportRedirects(PVOID ImageBase)
{
    USHORT Machine;

    if (!ChpeIsChpeProcess())
        return FALSE;

    Machine = ChpepGetImageMachine(ImageBase);
    return (Machine == IMAGE_FILE_MACHINE_AMD64 ||
            Machine == IMAGE_FILE_MACHINE_ARM64 ||
            Machine == IMAGE_FILE_MACHINE_ARM64EC);
}

static
BOOLEAN
ChpepSetEcCodePage(ULONG_PTR Page,
                   BOOLEAN Mark)
{
    PVOID BitmapBase;
    PULONGLONG Bitmap;
    ULONG_PTR Index;
    ULONGLONG Mask;

    BitmapBase = *ChpepGetPebEcCodeBitmapSlot();
    if (!BitmapBase)
        return FALSE;

    Index = Page / 64;
    if (((Index + 1) * sizeof(ULONGLONG)) > CHPE_EC_CODE_BITMAP_SIZE)
        return FALSE;

    Bitmap = (PULONGLONG)BitmapBase;
    Mask = 1ULL << (Page & 63);
    if (Mark)
        Bitmap[Index] |= Mask;
    else
        Bitmap[Index] &= ~Mask;

    return TRUE;
}

static
BOOLEAN
ChpepSetEcCodeRange(
    ULONG_PTR ImageBase,
    ULONG StartRva,
    ULONG Length,
    BOOLEAN Mark)
{
    ULONG_PTR Page, EndPage;

    if (!Length)
        return TRUE;

    if (!ChpepCommitEcCodeBitmapRange(ImageBase + StartRva, Length))
        return FALSE;

    Page = (ImageBase + StartRva) >> PAGE_SHIFT;
    EndPage = (ImageBase + StartRva + Length - 1) >> PAGE_SHIFT;

    for (; Page <= EndPage; ++Page)
    {
        if (!ChpepSetEcCodePage(Page, Mark))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
ChpepMarkEcCodeRange(
    ULONG_PTR ImageBase,
    ULONG StartRva,
    ULONG Length)
{
    return ChpepSetEcCodeRange(ImageBase, StartRva, Length, TRUE);
}

static
BOOLEAN
ChpepClearEcCodeRange(
    ULONG_PTR ImageBase,
    ULONG StartRva,
    ULONG Length)
{
    return ChpepSetEcCodeRange(ImageBase, StartRva, Length, FALSE);
}

BOOLEAN
NTAPI
ChpeMarkEcCodeRange(
    PVOID Address,
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
ChpepCommitImageExecuteSections(PVOID ImageBase,
                                PIMAGE_NT_HEADERS NtHeader,
                                BOOLEAN MarkAsEcCode)
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

        if (!ChpepCommitEcCodeBitmapRange((ULONG_PTR)ImageBase + StartRva,
                                          Length))
        {
            return FALSE;
        }

        if (!(MarkAsEcCode ?
              ChpepMarkEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length) :
              ChpepClearEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length)))
        {
            return FALSE;
        }
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
    if (!ImageBase || !Value || !Rva || SizeOfImage < sizeof(PVOID) ||
        Rva > SizeOfImage - sizeof(PVOID))
    {
        return FALSE;
    }

    return ChpepWritePointer((PBYTE)ImageBase + Rva, Value);
}

static
PVOID *
ChpepGetArm64EcPointerSlot(PVOID ImageBase,
                           ULONG SizeOfImage,
                           ULONG Rva)
{
    if (!ImageBase || !Rva || SizeOfImage < sizeof(PVOID) ||
        Rva > SizeOfImage - sizeof(PVOID))
    {
        return NULL;
    }

    return (PVOID *)((PBYTE)ImageBase + Rva);
}

static
VOID
__attribute__((naked))
ChpepArm64EcNoopCheck(VOID)
{
    __asm__ volatile("ret");
}

static
VOID
__attribute__((naked))
ChpepArm64EcCheckCall(VOID)
{
    __asm__ volatile(
        "ldr x16, [x18, #0x60]\n"        /* TEB->PEB */
        "ldr x16, [x16, #0x368]\n"       /* PEB->EcCodeBitMap */
        "cbz x16, 1f\n"
        "lsr x17, x11, #15\n"
        "and x17, x17, #0x1fffffffffff8\n"
        "ldr x16, [x16, x17]\n"
        "lsr x17, x11, #12\n"
        "lsr x16, x16, x17\n"
        "tbnz x16, #0, 1f\n"
        "mov x9, x11\n"                  /* x64 target for dispatch_call_no_redirect */
        "mov x11, x10\n"                 /* module-specific exit thunk */
        "1:\n"
        "ret\n");
}

static
VOID
ChpepPatchArm64EcDispatchHelpers(PVOID ImageBase,
                                 ULONG SizeOfImage,
                                 PIMAGE_ARM64EC_METADATA Metadata)
{
    PVOID *NoRedirectSlot;
    PVOID *RetSlot;
    PVOID NoRedirectBefore = NULL;
    PVOID RetBefore = NULL;
    PVOID NoRedirectAfter = NULL;
    PVOID RetAfter = NULL;
    BOOLEAN NoRedirectPatched = FALSE;
    BOOLEAN RetPatched = FALSE;
    static ULONG PatchLogCount;
    PVOID CheckTarget = ChpeEmulatorLoaded ?
                        (PVOID)ChpepArm64EcCheckCall :
                        (PVOID)ChpepArm64EcNoopCheck;

    NoRedirectSlot = ChpepGetArm64EcPointerSlot(ImageBase,
                                                SizeOfImage,
                                                Metadata->DispatchCallNoRedirect);
    RetSlot = ChpepGetArm64EcPointerSlot(ImageBase,
                                         SizeOfImage,
                                         Metadata->DispatchRet);
    if (NoRedirectSlot)
        NoRedirectBefore = *NoRedirectSlot;
    if (RetSlot)
        RetBefore = *RetSlot;

    ChpepPatchArm64EcPointer(ImageBase,
                             SizeOfImage,
                             Metadata->DispatchCall,
                             CheckTarget);
    ChpepPatchArm64EcPointer(ImageBase,
                             SizeOfImage,
                             Metadata->DispatchIcall,
                             CheckTarget);
    ChpepPatchArm64EcPointer(ImageBase,
                             SizeOfImage,
                             Metadata->DispatchIcallCfg,
                             CheckTarget);

    if (!ChpeEmulatorLoaded)
        return;

    NoRedirectPatched = ChpepPatchArm64EcPointer(ImageBase,
                                                 SizeOfImage,
                                                 Metadata->DispatchCallNoRedirect,
                                                 ChpeDispatchTable.ExitToX64);
    RetPatched = ChpepPatchArm64EcPointer(ImageBase,
                                          SizeOfImage,
                                          Metadata->DispatchRet,
                                          ChpeDispatchTable.RetToEntryThunk);
    ChpepPatchArm64EcPointer(ImageBase,
                             SizeOfImage,
                             Metadata->DispatchFptr,
                             ChpeDispatchTable.DispatchJump);

    if (NoRedirectSlot)
        NoRedirectAfter = *NoRedirectSlot;
    if (RetSlot)
        RetAfter = *RetSlot;

    if (PatchLogCount < 64 ||
        Metadata->DispatchCallNoRedirect == 0x8ffa0 ||
        SizeOfImage == 0xb0000)
    {
        PatchLogCount++;
        DPRINT1("CHPEPATCH base=%p size=%lx loaded=1 noRva=%lx noBefore=%p noAfter=%p noOk=%u exit=%p retRva=%lx retBefore=%p retAfter=%p retOk=%u retTarget=%p\n",
                ImageBase,
                SizeOfImage,
                Metadata->DispatchCallNoRedirect,
                NoRedirectBefore,
                NoRedirectAfter,
                NoRedirectPatched,
                ChpeDispatchTable.ExitToX64,
                Metadata->DispatchRet,
                RetBefore,
                RetAfter,
                RetPatched,
                ChpeDispatchTable.RetToEntryThunk);
    }
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

    NtHeader = RtlImageNtHeader(ImageBase);
    if (!NtHeader)
        return FALSE;

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    if (!NT_SUCCESS(ChpepEnsureProcessData()))
        return FALSE;

    ChpepPatchArm64EcDispatchHelpers(ImageBase,
                                     SizeOfImage,
                                     Metadata);

    Range = (PIMAGE_CHPE_RANGE_ENTRY)((PBYTE)ImageBase + Metadata->CodeMap);
    for (Index = 0; Index < Metadata->CodeMapCount; ++Index)
    {
        StartRva = Range[Index].StartOffset & ~1UL;
        Length = Range[Index].Length;

        if (!Length)
            continue;

        if (StartRva >= SizeOfImage || Length > SizeOfImage - StartRva)
            return FALSE;

        if (!ChpepCommitEcCodeBitmapRange((ULONG_PTR)ImageBase + StartRva,
                                          Length))
        {
            return FALSE;
        }

        if (!((Range[Index].StartOffset & 1) ?
              ChpepMarkEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length) :
              ChpepClearEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length)))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
VOID
ChpepClearImageCodeRanges(PVOID ImageBase)
{
    PIMAGE_ARM64EC_METADATA Metadata;
    PIMAGE_CHPE_RANGE_ENTRY Range;
    PIMAGE_NT_HEADERS NtHeader;
    PIMAGE_SECTION_HEADER Section;
    ULONG Index, StartRva, Length, SizeOfImage;

    NtHeader = RtlImageNtHeader(ImageBase);
    if (!NtHeader)
        return;

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    Metadata = ChpepGetArm64EcMetadata(ImageBase);
    if (Metadata)
    {
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
        return;
    }

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

        ChpepClearEcCodeRange((ULONG_PTR)ImageBase, StartRva, Length);
    }
}

BOOLEAN
NTAPI
ChpeRegisterImageCodeRanges(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;
    USHORT Machine;

    NtHeader = RtlImageNtHeader(ImageBase);
    if (!NtHeader)
        return FALSE;

    Machine = ChpepGetImageMachine(ImageBase);
    if (Machine == IMAGE_FILE_MACHINE_ARM64EC)
        return ChpeRegisterArm64EcImage(ImageBase);

    if (!NT_SUCCESS(ChpepEnsureProcessData()))
        return FALSE;

    if (Machine == IMAGE_FILE_MACHINE_AMD64)
        return ChpepCommitImageExecuteSections(ImageBase, NtHeader, FALSE);

    if (Machine == IMAGE_FILE_MACHINE_ARM64)
        return ChpepCommitImageExecuteSections(ImageBase, NtHeader, TRUE);

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
    for (ListEntry = ListHead->Flink;
         ListEntry != ListHead;
         ListEntry = ListEntry->Flink)
    {
        LdrEntry = CONTAINING_RECORD(ListEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);
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

    __asm__ volatile(
        "mov x0, %x[arg0]\n"
        "mov x1, %x[arg1]\n"
        "mov x2, %x[arg2]\n"
        "mov x3, %x[arg3]\n"
        "mov x9, %x[target]\n"
        "sub sp, sp, #0x20\n"
        "blr %x[dispatch]\n"
        "add sp, sp, #0x20\n"
        "mov %x[result], x0\n"
        : [result] "=r" (Result)
        : [arg0] "r" (Arg0),
          [arg1] "r" (Arg1),
          [arg2] "r" (Arg2),
          [arg3] "r" (Arg3),
          [target] "r" (EntryPoint),
          [dispatch] "r" (ChpeDispatchTable.ExitToX64)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
          "x8", "x9", "x10", "x11", "x12", "x15", "x16",
          "x17", "x30", "memory");

    return Result;
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
    Status = LdrGetProcedureAddress(Base, &name##Name, 0, (PVOID*)&field); \
    if (!NT_SUCCESS(Status)) { \
        DPRINT1("CHPE: Failed to resolve %Z, Status = 0x%08lx\n", &name##Name, Status); \
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

    ChpepRegisterNativeRuntimeImages();

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

    NtHeader = RtlImageNtHeader(Peb->ImageBaseAddress);
    if (NtHeader == NULL)
        return FALSE;

    return (NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 ||
            NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_ARM64EC);
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
    BOOLEAN LogException;
    static ULONG LogExceptionCount;

    LogException = (ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                    (((PARM64_NT_CONTEXT)Context)->Pc == 0 ||
                     ExceptionRecord->ExceptionInformation[1] == 0) &&
                    LogExceptionCount < 4);
    if (LogException)
        LogExceptionCount++;

    if (!ChpeEmulatorLoaded || !pChpeResetToConsistentState)
    {
        if (LogException)
        {
            DPRINT1("[CHPE] dispatch skipped: loaded=%u reset=%p code=0x%08lx addr=%p info0=%p info1=%p pc=%p\n",
                    ChpeEmulatorLoaded,
                    pChpeResetToConsistentState,
                    ExceptionRecord->ExceptionCode,
                    ExceptionRecord->ExceptionAddress,
                    (PVOID)ExceptionRecord->ExceptionInformation[0],
                    (PVOID)ExceptionRecord->ExceptionInformation[1],
                    (PVOID)((PARM64_NT_CONTEXT)Context)->Pc);
        }
        return FALSE;
    }

    CpuArea = ChpepGetCurrentCpuArea();
    if (!CpuArea || !CpuArea->EmulatorData[1] || !CpuArea->ContextAmd64)
    {
        if (LogException)
        {
            DPRINT1("[CHPE] dispatch skipped: cpu=%p emu1=%p ctxamd64=%p code=0x%08lx addr=%p info0=%p info1=%p pc=%p\n",
                    CpuArea,
                    CpuArea ? CpuArea->EmulatorData[1] : NULL,
                    CpuArea ? CpuArea->ContextAmd64 : NULL,
                    ExceptionRecord->ExceptionCode,
                    ExceptionRecord->ExceptionAddress,
                    (PVOID)ExceptionRecord->ExceptionInformation[0],
                    (PVOID)ExceptionRecord->ExceptionInformation[1],
                    (PVOID)((PARM64_NT_CONTEXT)Context)->Pc);
        }
        return FALSE;
    }

    if (LogException)
    {
        DPRINT1("[CHPE] dispatch FEX: code=0x%08lx addr=%p info0=%p info1=%p nativepc=%p emu1=%p ctxamd64=%p\n",
                ExceptionRecord->ExceptionCode,
                ExceptionRecord->ExceptionAddress,
                (PVOID)ExceptionRecord->ExceptionInformation[0],
                (PVOID)ExceptionRecord->ExceptionInformation[1],
                (PVOID)((PARM64_NT_CONTEXT)Context)->Pc,
                CpuArea->EmulatorData[1],
                CpuArea->ContextAmd64);
        if (((PARM64_NT_CONTEXT)Context)->Pc == 0)
        {
            ChpepDumpDispatchSlotsForAddress("lr",
                                             (PVOID)((PARM64_NT_CONTEXT)Context)->Lr);
            ChpepDumpDispatchSlotsForAddress("ntdll", &__ImageBase);
        }
    }

    pChpeResetToConsistentState(ExceptionRecord,
                                CpuArea->ContextAmd64,
                                Context /* Native ARM64 context */);

    if (LogException)
    {
        DPRINT1("[CHPE] FEX returned: code=0x%08lx addr=%p info0=%p info1=%p nativepc=%p\n",
                ExceptionRecord->ExceptionCode,
                ExceptionRecord->ExceptionAddress,
                (PVOID)ExceptionRecord->ExceptionInformation[0],
                (PVOID)ExceptionRecord->ExceptionInformation[1],
                (PVOID)((PARM64_NT_CONTEXT)Context)->Pc);
    }
    return FALSE;
}

BOOLEAN
NTAPI
ChpeShouldEmulateImage(PVOID ImageBase)
{
    PIMAGE_NT_HEADERS NtHeader;

    if (!ImageBase)
        return FALSE;

    NtHeader = RtlImageNtHeader(ImageBase);
    return (NtHeader &&
            NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
}

BOOLEAN
NTAPI
ChpeShouldRedirectImport(PVOID ImportBase,
                         PUNICODE_STRING ImportName)
{
    ULONG Index;

    if (!ImportName || !ImportName->Buffer)
        return FALSE;

    if (!ChpepNeedsChpeImportRedirects(ImportBase))
        return FALSE;

    for (Index = 0; Index < RTL_NUMBER_OF(ChpeArm64EcRedirectImports); Index++)
    {
        if (RtlEqualUnicodeString(ImportName,
                                  &ChpeArm64EcRedirectImports[Index],
                                  TRUE))
        {
            return TRUE;
        }
    }

    return FALSE;
}

NTSTATUS
NTAPI
ChpeValidateImportThunk(PVOID ImportBase,
                        PVOID ExportBase,
                        ULONG_PTR Function,
                        PCSTR DllName,
                        PCSTR ImportName,
                        ULONG Ordinal,
                        BOOLEAN IsOrdinal)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    USHORT ExportMachine;

    if (!ChpeIsChpeProcess())
        return STATUS_SUCCESS;

    if (!ChpepIsPureAmd64Image(ImportBase))
        return STATUS_SUCCESS;

    if (NT_SUCCESS(LdrFindEntryForAddress((PVOID)Function, &LdrEntry)))
        ExportBase = LdrEntry->DllBase;

    ExportMachine = ChpepGetImageMachine(ExportBase);
    if (ChpepIsX64CallableImageMachine(ExportMachine))
        return STATUS_SUCCESS;

    if (IsOrdinal)
    {
        DPRINT1("CHPE: refusing AMD64 import %s!#%lu at %p: "
                "target image machine 0x%04x is not x64-callable\n",
                DllName ? DllName : "<unknown>",
                Ordinal,
                (PVOID)Function,
                ExportMachine);
    }
    else
    {
        DPRINT1("CHPE: refusing AMD64 import %s!%s at %p: "
                "target image machine 0x%04x is not x64-callable\n",
                DllName ? DllName : "<unknown>",
                ImportName ? ImportName : "<unknown>",
                (PVOID)Function,
                ExportMachine);
    }

    return STATUS_INVALID_IMAGE_FORMAT;
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

    NtHeader = RtlImageNtHeader(ImageBase);
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

static
VOID
ChpepDumpDispatchSlotsForAddress(PCSTR Label, PVOID Address)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PIMAGE_ARM64EC_METADATA Metadata;
    PIMAGE_NT_HEADERS NtHeader;
    ULONG SizeOfImage;
    PVOID *NoRedirectSlot, *RetSlot;

    if (!Address ||
        !NT_SUCCESS(LdrFindEntryForAddress(Address, &LdrEntry)) ||
        !LdrEntry ||
        !LdrEntry->DllBase)
    {
        DPRINT1("[CHPE] pc0 %s address=%p no-loader-entry\n", Label, Address);
        return;
    }

    NtHeader = RtlImageNtHeader(LdrEntry->DllBase);
    Metadata = ChpepGetArm64EcMetadata(LdrEntry->DllBase);
    if (!NtHeader || !Metadata)
    {
        DPRINT1("[CHPE] pc0 %s address=%p base=%p name=%wZ no-chpe-metadata\n",
                Label,
                Address,
                LdrEntry->DllBase,
                &LdrEntry->BaseDllName);
        return;
    }

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    NoRedirectSlot = ChpepGetArm64EcPointerSlot(LdrEntry->DllBase,
                                                SizeOfImage,
                                                Metadata->DispatchCallNoRedirect);
    RetSlot = ChpepGetArm64EcPointerSlot(LdrEntry->DllBase,
                                         SizeOfImage,
                                         Metadata->DispatchRet);

    DPRINT1("[CHPE] pc0 %s address=%p base=%p name=%wZ noSlot=%p noValue=%p retSlot=%p retValue=%p\n",
            Label,
            Address,
            LdrEntry->DllBase,
            &LdrEntry->BaseDllName,
            NoRedirectSlot,
            NoRedirectSlot ? *NoRedirectSlot : NULL,
            RetSlot,
            RetSlot ? *RetSlot : NULL);
}

static
BOOLEAN
ChpepGetArm64EcNativeFunction(PVOID ExportBase,
                              ULONG_PTR Function,
                              PULONG_PTR NativeFunction)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PIMAGE_NT_HEADERS ExportNtHeader;
    ULONG_PTR FunctionRva, NativeFunctionRva;
    ULONG ExportSizeOfImage;

    if (!ExportBase || !Function || !NativeFunction)
        return FALSE;

    ExportNtHeader = RtlImageNtHeader(ExportBase);
    if (!ExportNtHeader ||
        Function < (ULONG_PTR)ExportBase ||
        Function - (ULONG_PTR)ExportBase >= ExportNtHeader->OptionalHeader.SizeOfImage)
    {
        if (!NT_SUCCESS(LdrFindEntryForAddress((PVOID)Function, &LdrEntry)))
            return FALSE;

        ExportBase = LdrEntry->DllBase;
        ExportNtHeader = RtlImageNtHeader(ExportBase);
        if (!ExportNtHeader)
            return FALSE;
    }

    if (ChpepGetImageMachine(ExportBase) != IMAGE_FILE_MACHINE_ARM64EC)
        return FALSE;

    ExportSizeOfImage = ExportNtHeader->OptionalHeader.SizeOfImage;
    FunctionRva = Function - (ULONG_PTR)ExportBase;
    if (FunctionRva >= ExportSizeOfImage ||
        !ChpeGetArm64EcRedirection(ExportBase,
                                   FunctionRva,
                                   &NativeFunctionRva))
    {
        return FALSE;
    }

    *NativeFunction = (ULONG_PTR)ExportBase + NativeFunctionRva;
    ChpeRegisterArm64EcImage(ExportBase);
    return TRUE;
}

BOOLEAN
NTAPI
ChpePatchArm64ImportThunk(PVOID ImportBase,
                          PVOID ExportBase,
                          PIMAGE_THUNK_DATA Thunk)
{
    ULONG_PTR NativeFunction;

    if (!Thunk || !Thunk->u1.Function ||
        ChpepGetImageMachine(ImportBase) != IMAGE_FILE_MACHINE_ARM64)
    {
        return FALSE;
    }

    if (!ChpepGetArm64EcNativeFunction(ExportBase,
                                       Thunk->u1.Function,
                                       &NativeFunction))
    {
        return FALSE;
    }

    if (NativeFunction == Thunk->u1.Function)
        return TRUE;

    return ChpepWritePointer(&Thunk->u1.Function, (PVOID)NativeFunction);
}

BOOLEAN
NTAPI
ChpePatchArm64EcAuxiliaryIat(PVOID ImportBase,
                             PVOID ExportBase,
                             PIMAGE_THUNK_DATA Thunk,
                             ULONG_PTR Function)
{
    PIMAGE_NT_HEADERS ImportNtHeader;
    PIMAGE_DATA_DIRECTORY IatDirectory;
    PIMAGE_ARM64EC_METADATA ImportMetadata;
    ULONG ImportSizeOfImage;
    ULONG IatRva, IatSize;
    ULONG_PTR ThunkRva, AuxiliaryIatRva, ThunkOffset;
    ULONG_PTR NativeFunction;
    BOOLEAN Patched;
    USHORT ImportMachine;

    ImportMachine = ChpepGetImageMachine(ImportBase);
    if (!Thunk || !Function || ImportMachine != IMAGE_FILE_MACHINE_ARM64EC)
        return FALSE;

    ImportNtHeader = RtlImageNtHeader(ImportBase);
    ImportMetadata = ChpepGetArm64EcMetadata(ImportBase);
    if (!ImportNtHeader || !ImportMetadata || !ImportMetadata->AuxiliaryIat)
        return FALSE;

    ImportSizeOfImage = ImportNtHeader->OptionalHeader.SizeOfImage;
    IatDirectory = &ImportNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    IatRva = IatDirectory->VirtualAddress;
    IatSize = IatDirectory->Size;
    if (!IatRva || IatSize < sizeof(PVOID) ||
        IatRva >= ImportSizeOfImage ||
        IatSize > ImportSizeOfImage - IatRva)
        return FALSE;

    if ((ULONG_PTR)Thunk < (ULONG_PTR)ImportBase)
        return FALSE;

    ThunkRva = (ULONG_PTR)Thunk - (ULONG_PTR)ImportBase;
    if (ThunkRva < IatRva ||
        ThunkRva > IatRva + IatSize - sizeof(PVOID))
        return FALSE;

    ThunkOffset = ThunkRva - IatRva;
    AuxiliaryIatRva = ImportMetadata->AuxiliaryIat + ThunkOffset;
    if (AuxiliaryIatRva < ImportMetadata->AuxiliaryIat ||
        AuxiliaryIatRva > ImportSizeOfImage - sizeof(PVOID))
        return FALSE;

    NativeFunction = Function;
    ChpepGetArm64EcNativeFunction(ExportBase, Function, &NativeFunction);

    ChpeRegisterArm64EcImage(ImportBase);
    Patched = ChpepWritePointer((PBYTE)ImportBase + AuxiliaryIatRva,
                                (PVOID)NativeFunction);

    if (ImportMetadata->AuxiliaryIatCopy)
    {
        AuxiliaryIatRva = ImportMetadata->AuxiliaryIatCopy + ThunkOffset;
        if (AuxiliaryIatRva >= ImportMetadata->AuxiliaryIatCopy &&
            AuxiliaryIatRva <= ImportSizeOfImage - sizeof(PVOID))
        {
            Patched |= ChpepWritePointer((PBYTE)ImportBase + AuxiliaryIatRva,
                                         (PVOID)NativeFunction);
        }
    }

    return Patched;
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

    if (!ChpeEmulatorLoaded ||
        pChpeNotifyMemoryAlloc == NULL)
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

    if (!ChpeEmulatorLoaded ||
        pChpeNotifyMemoryFree == NULL)
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

    if (!ChpeEmulatorLoaded ||
        pChpeNotifyMemoryProtect == NULL)
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
    NTSTATUS Status;
    PCHPE_V2_CPU_AREA_INFO CpuArea;

    if (!ChpeEmulatorLoaded ||
        pChpeNotifyMapViewOfSection == NULL)
        return STATUS_SUCCESS;

    ChpeRegisterImageCodeRanges(Address);

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

    if (!ChpeEmulatorLoaded ||
        pChpeNotifyUnmapViewOfSection == NULL)
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

    if (!ChpeEmulatorLoaded ||
        pChpeFlushInstructionCache == NULL)
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

#endif /* _M_ARM64 || _M_ARM64EC */
