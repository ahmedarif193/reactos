/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS System Libraries
 * FILE:            dll/ntdll/include/ntdllp.h
 * PURPOSE:         Native Library Internal Header
 * PROGRAMMER:      Alex Ionescu (alex@relsoft.net)
 */

#pragma once

#define LDR_HASH_TABLE_ENTRIES 32
#define LDR_GET_HASH_ENTRY(x) (RtlUpcaseUnicodeChar((x)) & (LDR_HASH_TABLE_ENTRIES - 1))

/* LdrpUpdateLoadCount2 flags */
#define LDRP_UPDATE_REFCOUNT   0x01
#define LDRP_UPDATE_DEREFCOUNT 0x02
#define LDRP_UPDATE_PIN        0x03

/* Loader flags */
#define IMAGE_LOADER_FLAGS_COMPLUS 0x00000001
#define IMAGE_LOADER_FLAGS_SYSTEM_GLOBAL 0x01000000

/* Page heap flags */
#define DPH_FLAG_DLL_NOTIFY 0x40

typedef struct _LDRP_TLS_DATA
{
    LIST_ENTRY TlsLinks;
    IMAGE_TLS_DIRECTORY TlsDirectory;
    PVOID DllBase;
    BOOLEAN Active;
} LDRP_TLS_DATA, *PLDRP_TLS_DATA;

typedef VOID (NTAPI *PRTLP_FLS_CALLBACK_DISPATCHER)(PFLS_CALLBACK_FUNCTION Callback, PVOID Data);

VOID
NTAPI
RtlpSetFlsCallbackDispatcher(
    _In_opt_ PRTLP_FLS_CALLBACK_DISPATCHER Dispatcher);

VOID
NTAPI
RtlpCallFlsCallback(
    _In_ PFLS_CALLBACK_FUNCTION Callback,
    _In_opt_ PVOID Data);

typedef VOID (NTAPI *PRTLP_THREADPOOL_CALLBACK_DISPATCHER)(PVOID Callback, ULONG_PTR Argument0, ULONG_PTR Argument1, ULONG_PTR Argument2, ULONG_PTR Argument3);

VOID
NTAPI
RtlpSetThreadpoolCallbackDispatcher(
    _In_opt_ PRTLP_THREADPOOL_CALLBACK_DISPATCHER Dispatcher);

typedef
NTSTATUS
(NTAPI* PLDR_APP_COMPAT_DLL_REDIRECTION_CALLBACK_FUNCTION)(
    _In_ ULONG Flags,
    _In_ PCWSTR DllName,
    _In_ PCWSTR DllPath OPTIONAL,
    _Inout_opt_ PULONG DllCharacteristics,
    _In_ PVOID CallbackData,
    _Outptr_ PWSTR* EffectiveDllPath);

/* Global data */
extern RTL_CRITICAL_SECTION LdrpLoaderLock;
extern BOOLEAN LdrpInLdrInit;
extern PVOID LdrpHeap;
extern LIST_ENTRY LdrpHashTable[LDR_HASH_TABLE_ENTRIES];
extern BOOLEAN ShowSnaps;
extern UNICODE_STRING LdrpDefaultPath;
extern HANDLE LdrpKnownDllObjectDirectory;
extern ULONG LdrpNumberOfProcessors;
extern ULONG LdrpFatalHardErrorCount;
extern PUNICODE_STRING LdrpTopLevelDllBeingLoaded;
extern PLDR_DATA_TABLE_ENTRY LdrpCurrentDllInitializer;
extern UNICODE_STRING LdrApiDefaultExtension;
extern BOOLEAN LdrpLdrDatabaseIsSetup;
extern ULONG LdrpActiveUnloadCount;
extern BOOLEAN LdrpShutdownInProgress;
extern UNICODE_STRING LdrpKnownDllPath;
extern PLDR_DATA_TABLE_ENTRY LdrpGetModuleHandleCache, LdrpLoadedDllHandleCache;
extern BOOLEAN RtlpPageHeapEnabled;
extern ULONG RtlpDphGlobalFlags;
extern BOOLEAN g_ShimsEnabled;
extern PVOID g_pShimEngineModule;
extern PVOID g_pfnSE_DllLoaded;
extern PVOID g_pfnSE_DllUnloaded;
extern PVOID g_pfnSE_InstallBeforeInit;
extern PVOID g_pfnSE_InstallAfterInit;
extern PVOID g_pfnSE_ProcessDying;

NTSTATUS
RtlpGetExtendedParameterZeroBits(
    _In_reads_opt_(ExtendedParameterCount) PMEM_EXTENDED_PARAMETER ExtendedParameters,
    _In_ ULONG ExtendedParameterCount,
    _Out_ PULONG_PTR ZeroBits,
    _Out_ PBOOLEAN EcCode);

/* ldrinit.c */
NTSTATUS NTAPI LdrpRunInitializeRoutines(IN PCONTEXT Context OPTIONAL);
VOID NTAPI LdrpInitializeThread(IN PCONTEXT Context);
NTSTATUS NTAPI LdrpInitializeTls(VOID);
NTSTATUS NTAPI LdrpHandleTlsData(IN PLDR_DATA_TABLE_ENTRY LdrEntry);
VOID NTAPI LdrpReleaseTlsData(IN PLDR_DATA_TABLE_ENTRY LdrEntry);
NTSTATUS NTAPI LdrpAllocateTls(VOID);
VOID NTAPI LdrpFreeTls(VOID);
VOID NTAPI LdrpCallTlsInitializers(IN PLDR_DATA_TABLE_ENTRY LdrEntry, IN ULONG Reason);
BOOLEAN NTAPI LdrpCallInitRoutine(IN PDLL_INIT_ROUTINE EntryPoint, IN PVOID BaseAddress, IN ULONG Reason, IN PVOID Context);
LONG NTAPI LdrpDebugExceptionFilter(IN PEXCEPTION_POINTERS ExceptionPointers);
NTSTATUS NTAPI LdrpInitializeProcess(IN PCONTEXT Context, IN PVOID SystemArgument1);
VOID NTAPI LdrpInitFailure(NTSTATUS Status);
VOID NTAPI LdrpValidateImageForMp(IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry);
VOID NTAPI LdrpEnsureLoaderLockIsHeld(VOID);

/* ldrpe.c */
NTSTATUS
NTAPI
LdrpSnapThunk(IN PVOID ExportBase,
              IN PVOID ImportBase,
              IN PIMAGE_THUNK_DATA OriginalThunk,
              IN OUT PIMAGE_THUNK_DATA Thunk,
              IN PIMAGE_EXPORT_DIRECTORY ExportEntry,
              IN ULONG ExportSize,
              IN BOOLEAN Static,
              IN LPSTR DllName);

NTSTATUS NTAPI
LdrpWalkImportDescriptor(IN LPWSTR DllPath OPTIONAL,
                         IN PLDR_DATA_TABLE_ENTRY LdrEntry);

/* libsupp.c */
NTSYSAPI
NTSTATUS
NTAPI
LdrpApplyFileNameRedirection(
    _In_ PUNICODE_STRING OriginalName,
    _In_ PUNICODE_STRING Extension,
    _Inout_opt_ PUNICODE_STRING StaticString,
    _Inout_opt_ PUNICODE_STRING DynamicString,
    _Inout_ PUNICODE_STRING *NewName,
    _Out_ PBOOLEAN RedirectedDll,
    _Out_opt_ PBOOLEAN ApiSetRedirected);

/* ldrutils.c */
PVOID NTAPI
RtlFindExportedRoutineByName(
    _In_ PVOID BaseAddress,
    _In_ PCSTR RoutineName);

NTSTATUS NTAPI
LdrpCreateDllSection(IN PUNICODE_STRING FullName,
                     IN HANDLE DllHandle,
                     IN PULONG DllCharacteristics OPTIONAL,
                     OUT PHANDLE SectionHandle);

#if defined(_M_ARM64)
NTSTATUS
LdrpBuildArm64EcImportName(
    PUNICODE_STRING ImportName,
    PUNICODE_STRING RedirectedImportName);
#endif

NTSTATUS
NTAPI
LdrpGetProcedureAddress(
    _In_ PVOID BaseAddress,
    _In_opt_ _When_(Ordinal == 0, _Notnull_) PANSI_STRING Name,
    _In_opt_ _When_(Name == NULL, _In_range_(>, 0)) ULONG Ordinal,
    _Out_ PVOID *ProcedureAddress,
    _In_ BOOLEAN ExecuteInit);

PLDR_DATA_TABLE_ENTRY NTAPI
LdrpAllocateDataTableEntry(IN PVOID BaseAddress);

VOID NTAPI
LdrpInsertMemoryTableEntry(IN PLDR_DATA_TABLE_ENTRY LdrEntry);

NTSTATUS NTAPI
LdrpLoadDll(IN BOOLEAN Redirected,
            IN PWSTR DllPath OPTIONAL,
            IN PULONG DllCharacteristics OPTIONAL,
            IN PUNICODE_STRING DllName,
            OUT PVOID *BaseAddress,
            IN BOOLEAN CallInit);

VOID NTAPI
LdrpUpdateLoadCount2(IN PLDR_DATA_TABLE_ENTRY LdrEntry,
                     IN ULONG Flags);

ULONG NTAPI
LdrpClearLoadInProgress(VOID);

NTSTATUS
NTAPI
LdrpSetProtection(PVOID ViewBase,
                  BOOLEAN Restore);

BOOLEAN
NTAPI
LdrpCheckForLoadedDllHandle(IN PVOID Base,
                            OUT PLDR_DATA_TABLE_ENTRY *LdrEntry);

BOOLEAN NTAPI
LdrpCheckForLoadedDll(IN PWSTR DllPath,
                      IN PUNICODE_STRING DllName,
                      IN BOOLEAN Flag,
                      IN BOOLEAN RedirectedDll,
                      OUT PLDR_DATA_TABLE_ENTRY *LdrEntry);

NTSTATUS NTAPI
LdrpMapDll(IN PWSTR SearchPath OPTIONAL,
           IN PWSTR DllPath2,
           IN PWSTR DllName OPTIONAL,
           IN PULONG DllCharacteristics,
           IN BOOLEAN Static,
           IN BOOLEAN Redirect,
           OUT PLDR_DATA_TABLE_ENTRY *DataTableEntry);

PVOID NTAPI
LdrpFetchAddressOfEntryPoint(PVOID ImageBase);

VOID NTAPI
LdrpFreeUnicodeString(PUNICODE_STRING String);

VOID NTAPI
LdrpRecordUnloadEvent(_In_ PLDR_DATA_TABLE_ENTRY LdrEntry);

VOID NTAPI
LdrpGetShimEngineInterface(VOID);

VOID
NTAPI
LdrpLoadShimEngine(IN PWSTR ImageName,
                   IN PUNICODE_STRING ProcessImage,
                   IN PVOID pShimData);

VOID NTAPI
LdrpUnloadShimEngine(VOID);

/* verifier.c */

NTSTATUS NTAPI
LdrpInitializeApplicationVerifierPackage(IN HANDLE KeyHandle,
                                         IN PPEB Peb,
                                         IN BOOLEAN SystemWide,
                                         IN BOOLEAN ReadAdvancedOptions);

NTSTATUS NTAPI
AVrfInitializeVerifier(VOID);

VOID NTAPI
AVrfDllLoadNotification(IN PLDR_DATA_TABLE_ENTRY LdrEntry);

VOID NTAPI
AVrfDllUnloadNotification(IN PLDR_DATA_TABLE_ENTRY LdrEntry);

VOID NTAPI
AVrfPageHeapDllNotification(IN PLDR_DATA_TABLE_ENTRY LdrEntry);


/* FIXME: Cleanup this mess */
typedef NTSTATUS (NTAPI *PEPFUNC)(PPEB);
NTSTATUS LdrMapSections(HANDLE ProcessHandle,
                        PVOID ImageBase,
                        HANDLE SectionHandle,
                        PIMAGE_NT_HEADERS NTHeaders);
NTSTATUS LdrMapNTDllForProcess(HANDLE ProcessHandle,
                               PHANDLE NTDllSectionHandle);
ULONG
LdrpGetResidentSize(PIMAGE_NT_HEADERS NTHeaders);

NTSTATUS
NTAPI
LdrpLoadImportModule(IN PWSTR DllPath OPTIONAL,
                     IN PVOID ImportBase,
                     IN LPSTR ImportName,
                     OUT PLDR_DATA_TABLE_ENTRY *DataTableEntry,
                     OUT PBOOLEAN Existing);

VOID
NTAPI
LdrpFinalizeAndDeallocateDataTableEntry(IN PLDR_DATA_TABLE_ENTRY Entry);

#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA) || (DLL_EXPORT_VERSION >= _WIN32_WINNT_VISTA)

VOID
NTAPI
LdrpSendDllNotifications(
    _In_ PLDR_DATA_TABLE_ENTRY DllEntry,
    _In_ ULONG NotificationReason);

#endif /* (_WIN32_WINNT >= _WIN32_WINNT_VISTA) || (DLL_EXPORT_VERSION >= _WIN32_WINNT_VISTA) */

/* path.c */
BOOLEAN
NTAPI
RtlDoesFileExists_UStr(
    IN PUNICODE_STRING FileName
);

VOID
NTAPI
RtlpInitializeThreadPooling(
    VOID);

/* CHPE (ARM64EC) emulator integration -- chpe.c */
#if defined(_M_ARM64)

BOOLEAN
NTAPI
ChpeIsChpeProcess(
    VOID);

BOOLEAN
NTAPI
ChpeIsEmulatorReady(
    VOID);

PVOID
NTAPI
ChpeEnterEmulatorCallback(
    VOID);

VOID
NTAPI
ChpeLeaveEmulatorCallback(
    PVOID CallbackToken);

NTSTATUS
NTAPI
ChpeInitializeProcess(
    VOID);

NTSTATUS
NTAPI
ChpeInitializeThread(
    VOID);

VOID
NTAPI
ChpeCleanupThread(
    HANDLE ThreadHandle,
    LONG ExitCode);

NTSTATUS
NTAPI
ChpePrepareThreadTermination(
    HANDLE ThreadHandle,
    LONG ExitCode);

VOID
NTAPI
ChpeNotifyProcessTermination(
    HANDLE ProcessHandle,
    BOOLEAN After,
    NTSTATUS Status);

VOID
NTAPI
ChpeCleanupProcess(
    HANDLE ProcessHandle,
    NTSTATUS ExitStatus);

BOOLEAN
NTAPI
ChpeDispatchException(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT Context);

VOID
NTAPI
ChpeNotifyMemoryAlloc(
    PVOID Address,
    SIZE_T Size,
    ULONG Type,
    ULONG Prot,
    BOOLEAN After,
    NTSTATUS Status);

VOID
NTAPI
ChpeNotifyMemoryFree(
    PVOID Address,
    SIZE_T Size,
    ULONG FreeType,
    BOOLEAN After,
    NTSTATUS Status);

VOID
NTAPI
ChpeNotifyMemoryProtect(
    PVOID Address,
    SIZE_T Size,
    ULONG NewProt,
    BOOLEAN After,
    NTSTATUS Status);

NTSTATUS
NTAPI
ChpeNotifyMapViewOfSection(
    PVOID Unk1,
    PVOID Address,
    PVOID Unk2,
    SIZE_T Size,
    ULONG AllocType,
    ULONG Prot);

VOID
NTAPI
ChpeNotifyUnmapViewOfSection(
    PVOID Address,
    BOOLEAN After,
    NTSTATUS Status);

VOID
NTAPI
ChpeFlushInstructionCache(
    const void *Address,
    SIZE_T Size);

VOID
NTAPI
ChpeNotifyMemoryDirty(
    PVOID Address,
    SIZE_T Size);

VOID
NTAPI
ChpeNotifyReadFile(
    HANDLE FileHandle,
    PVOID Address,
    SIZE_T Size,
    BOOLEAN After,
    NTSTATUS Status);

BOOLEAN
NTAPI
ChpeIsProcessorFeaturePresent(
    ULONG ProcessorFeature);

VOID
NTAPI
ChpeUpdateProcessorInformation(
    PVOID ProcessorInformation);

BOOLEAN
NTAPI
ChpeShouldEmulateImage(
    PVOID ImageBase);

USHORT
NTAPI
ChpeGetImageMachine(
    PVOID ImageBase);

BOOLEAN
NTAPI
ChpeShouldRedirectImport(
    PVOID ImportBase,
    PUNICODE_STRING ImportName);

BOOLEAN
NTAPI
ChpeShouldRedirectDynamicLoad(
    PUNICODE_STRING DllName);

NTSTATUS
NTAPI
ChpeValidateImportThunk(
    PVOID ImportBase,
    PVOID ExportBase,
    ULONG_PTR Function,
    PCSTR DllName,
    PCSTR ImportName,
    ULONG Ordinal,
    BOOLEAN IsOrdinal);

BOOLEAN
NTAPI
ChpeRegisterArm64EcImage(
    PVOID ImageBase);

BOOLEAN
NTAPI
ChpeRegisterImageCodeRanges(
    PVOID ImageBase);

BOOLEAN
NTAPI
ChpeMarkEcCodeRange(
    PVOID Address,
    SIZE_T Length);

BOOLEAN
NTAPI
RtlIsEcCode(
    ULONG_PTR CodeAddress);

BOOLEAN
NTAPI
ChpeGetArm64EcRedirection(
    PVOID ImageBase,
    ULONG_PTR SourceRva,
    PULONG_PTR DestinationRva);

BOOLEAN
NTAPI
ChpeGetArm64EcNativeFunction(
    PVOID ExportBase,
    ULONG_PTR Function,
    PULONG_PTR NativeFunction);

BOOLEAN
NTAPI
ChpePatchArm64EcAuxiliaryIat(
    PVOID ImportBase,
    PVOID ExportBase,
    PIMAGE_THUNK_DATA Thunk,
    ULONG_PTR Function);

BOOLEAN
NTAPI
ChpeCallX64DllMain(
    PVOID EntryPoint,
    PVOID BaseAddress,
    ULONG Reason,
    PVOID Context);

VOID
NTAPI
RtlUserThreadStart(
    PVOID StartAddress,
    PVOID Parameter);

VOID
NTAPI
ChpeRtlUserThreadStart(
    PVOID StartAddress,
    PVOID Parameter);

#endif /* _M_ARM64 */

/* EOF */
