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
#define CHPE_EC_CODE_BITMAP_SIZE 0x100000

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

static const UNICODE_STRING ChpeDllName = RTL_CONSTANT_STRING(L"arm64ecfex.dll");

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
PVOID *
ChpepGetPebEcCodeBitmapSlot(VOID)
{
    return (PVOID *)((PBYTE)NtCurrentPeb() + CHPE_PEB_EC_CODE_BITMAP_OFFSET);
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

    Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                     &EcCodeBitmap,
                                     0,
                                     &RegionSize,
                                     MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(EcCodeBitmap, CHPE_EC_CODE_BITMAP_SIZE);
    ChpeEcCodeBitmap = EcCodeBitmap;
    *EcCodeBitmapSlot = ChpeEcCodeBitmap;
    return STATUS_SUCCESS;
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

        NtFreeVirtualMemory(NtCurrentProcess(),
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
    PVOID LoadConfig;
    ULONG ConfigSize, Offset, SizeOfImage;
    ULONG_PTR ImageStart, ImageEnd, Candidate;
    PIMAGE_ARM64EC_METADATA Metadata;

    if (!ImageBase)
        return NULL;

    NtHeader = RtlImageNtHeader(ImageBase);
    if (!NtHeader ||
        NtHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
    {
        return NULL;
    }

    SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    ImageStart = (ULONG_PTR)ImageBase;
    ImageEnd = ImageStart + SizeOfImage;

    LoadConfig = RtlImageDirectoryEntryToData(ImageBase,
                                              TRUE,
                                              IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG,
                                              &ConfigSize);
    if (!LoadConfig)
        return NULL;

    for (Offset = 0; Offset + sizeof(PVOID) <= ConfigSize; Offset += sizeof(PVOID))
    {
        Candidate = *(PULONG_PTR)((PBYTE)LoadConfig + Offset);
        if (Candidate < ImageStart ||
            Candidate > ImageEnd - sizeof(IMAGE_ARM64EC_METADATA))
        {
            continue;
        }

        Metadata = (PIMAGE_ARM64EC_METADATA)Candidate;
        if (Metadata->Version != 1 ||
            !Metadata->CodeMap ||
            !Metadata->CodeMapCount ||
            Metadata->CodeMap >= SizeOfImage ||
            Metadata->CodeMapCount >
            (SizeOfImage - Metadata->CodeMap) / sizeof(IMAGE_CHPE_RANGE_ENTRY))
        {
            continue;
        }

        return Metadata;
    }

    return NULL;
}

static
VOID
ChpepMarkEcCodePage(ULONG_PTR Page)
{
    PVOID BitmapBase;
    PULONGLONG Bitmap;
    ULONG_PTR Index;

    BitmapBase = *ChpepGetPebEcCodeBitmapSlot();
    if (!BitmapBase)
        return;

    Index = Page / 64;
    if (((Index + 1) * sizeof(ULONGLONG)) > CHPE_EC_CODE_BITMAP_SIZE)
        return;

    Bitmap = (PULONGLONG)BitmapBase;
    Bitmap[Index] |= 1ULL << (Page & 63);
}

static
VOID
ChpepMarkEcCodeRange(
    ULONG_PTR ImageBase,
    ULONG StartRva,
    ULONG Length)
{
    ULONG_PTR Page, EndPage;

    if (!Length)
        return;

    Page = (ImageBase + StartRva) >> PAGE_SHIFT;
    EndPage = (ImageBase + StartRva + Length - 1) >> PAGE_SHIFT;

    for (; Page <= EndPage; ++Page)
    {
        ChpepMarkEcCodePage(Page);
    }
}

static
VOID
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
        return;

    *(PVOID *)Address = Value;

    ProtectBase = Address;
    ProtectSize = sizeof(PVOID);
    NtProtectVirtualMemory(NtCurrentProcess(),
                           &ProtectBase,
                           &ProtectSize,
                           OldProtect,
                           &IgnoredProtect);
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
ChpepPatchArm64EcDispatchHelpers(PVOID ImageBase,
                                 ULONG SizeOfImage,
                                 PIMAGE_ARM64EC_METADATA Metadata)
{
    if (Metadata->DispatchCall &&
        Metadata->DispatchCall <= SizeOfImage - sizeof(PVOID))
    {
        ChpepWritePointer((PBYTE)ImageBase + Metadata->DispatchCall,
                          ChpepArm64EcNoopCheck);
    }

    if (Metadata->DispatchIcall &&
        Metadata->DispatchIcall <= SizeOfImage - sizeof(PVOID))
    {
        ChpepWritePointer((PBYTE)ImageBase + Metadata->DispatchIcall,
                          ChpepArm64EcNoopCheck);
    }

    if (Metadata->DispatchIcallCfg &&
        Metadata->DispatchIcallCfg <= SizeOfImage - sizeof(PVOID))
    {
        ChpepWritePointer((PBYTE)ImageBase + Metadata->DispatchIcallCfg,
                          ChpepArm64EcNoopCheck);
    }
}

BOOLEAN
NTAPI
ChpeRegisterArm64EcImage(PVOID ImageBase)
{
    PIMAGE_ARM64EC_METADATA Metadata;
    PIMAGE_CHPE_RANGE_ENTRY Range;
    PIMAGE_NT_HEADERS NtHeader;
    ULONG Index;

    Metadata = ChpepGetArm64EcMetadata(ImageBase);
    if (!Metadata)
        return FALSE;

    NtHeader = RtlImageNtHeader(ImageBase);
    if (!NtHeader)
        return FALSE;

    if (!NT_SUCCESS(ChpepEnsureProcessData()))
        return FALSE;

    ChpepPatchArm64EcDispatchHelpers(ImageBase,
                                     NtHeader->OptionalHeader.SizeOfImage,
                                     Metadata);

    Range = (PIMAGE_CHPE_RANGE_ENTRY)((PBYTE)ImageBase + Metadata->CodeMap);
    for (Index = 0; Index < Metadata->CodeMapCount; ++Index)
    {
        if (Range[Index].StartOffset & 1)
        {
            ChpepMarkEcCodeRange((ULONG_PTR)ImageBase,
                                 Range[Index].StartOffset & ~1UL,
                                 Range[Index].Length);
        }
    }

    return TRUE;
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
          "x8", "x9", "x10", "x11", "x12", "x13", "x14",
          "x15", "x16", "x17", "x30", "memory");

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

    Status = pChpeProcessInit();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[CHPE] ntdll: ProcessInit failed, Status = 0x%08lx\n", Status);
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

    NtHeader = RtlImageNtHeader(Peb->ImageBaseAddress);
    if (NtHeader == NULL)
        return FALSE;

    return (NtHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
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
    if (!ChpeEmulatorLoaded)
        return FALSE;

    if (!ChpepIsCurrentThreadInitialized())
        return FALSE;

    pChpeResetToConsistentState(ExceptionRecord,
                                Context,
                                Context /* Native ARM64 context */);
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
    if (!ChpeEmulatorLoaded || pChpeNotifyMemoryAlloc == NULL)
        return;
    pChpeNotifyMemoryAlloc(Address, Size, Type, Prot, After, Status);
}

/*
 * Notify the CHPE emulator of a memory free.
 */
VOID
NTAPI
ChpeNotifyMemoryFree(PVOID Address, SIZE_T Size, ULONG FreeType,
                     BOOLEAN After, NTSTATUS Status)
{
    if (!ChpeEmulatorLoaded || pChpeNotifyMemoryFree == NULL)
        return;
    pChpeNotifyMemoryFree(Address, Size, FreeType, After, Status);
}

/*
 * Notify the CHPE emulator of a memory protection change.
 */
VOID
NTAPI
ChpeNotifyMemoryProtect(PVOID Address, SIZE_T Size, ULONG NewProt,
                        BOOLEAN After, NTSTATUS Status)
{
    if (!ChpeEmulatorLoaded || pChpeNotifyMemoryProtect == NULL)
        return;
    pChpeNotifyMemoryProtect(Address, Size, NewProt, After, Status);
}

/*
 * Notify the CHPE emulator of a section mapping.
 */
NTSTATUS
NTAPI
ChpeNotifyMapViewOfSection(PVOID Unk1, PVOID Address, PVOID Unk2,
                           SIZE_T Size, ULONG AllocType, ULONG Prot)
{
    if (!ChpeEmulatorLoaded || pChpeNotifyMapViewOfSection == NULL)
        return STATUS_SUCCESS;
    return pChpeNotifyMapViewOfSection(Unk1, Address, Unk2, Size, AllocType, Prot);
}

/*
 * Notify the CHPE emulator of a section unmapping.
 */
VOID
NTAPI
ChpeNotifyUnmapViewOfSection(PVOID Address, BOOLEAN After, NTSTATUS Status)
{
    if (!ChpeEmulatorLoaded || pChpeNotifyUnmapViewOfSection == NULL)
        return;
    pChpeNotifyUnmapViewOfSection(Address, After, Status);
}

/*
 * Flush the instruction cache for a range.
 */
VOID
NTAPI
ChpeFlushInstructionCache(const void *Address, SIZE_T Size)
{
    if (!ChpeEmulatorLoaded || pChpeFlushInstructionCache == NULL)
        return;
    pChpeFlushInstructionCache(Address, Size);
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
