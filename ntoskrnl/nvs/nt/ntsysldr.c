/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntsysldr.c
 * PURPOSE:     Kernel system image loading and unloading
 */

#include <ntoskrnl.h>
#include <apisets.h>
#define NDEBUG
#include <debug.h>

#include <nvs/nt/mint.h>


LIST_ENTRY PsLoadedModuleList;
LIST_ENTRY MmLoadedUserImageList;
KSPIN_LOCK PsLoadedModuleSpinLock;
ERESOURCE PsLoadedModuleResource;
ULONG_PTR PsNtosImageBase;
KMUTANT MmSystemLoadLock;

PFN_NUMBER MmTotalSystemDriverPages;

PVOID MmUnloadedDrivers;
PVOID MmLastUnloadedDrivers;

BOOLEAN MmMakeLowMemory;
BOOLEAN MmEnforceWriteProtection = TRUE;
extern UCHAR MmDisablePagingExecutive;

static ULONG64 MiKernelResourceStart, MiKernelResourceEnd;
ULONG_PTR ExPoolCodeStart, ExPoolCodeEnd, MmPoolCodeStart, MmPoolCodeEnd;
ULONG_PTR MmPteCodeStart, MmPteCodeEnd;

#ifdef _WIN64
#define COOKIE_MAX 0x0000FFFFFFFFFFFFll
#define DEFAULT_SECURITY_COOKIE 0x00002B992DDFA232ll
#else
#define DEFAULT_SECURITY_COOKIE 0xBB40E64E
#endif

PVOID
NTAPI
MiCacheImageSymbols(IN PVOID BaseAddress)
{
    ULONG DebugSize;
    PVOID DebugDirectory = NULL;
    PAGED_CODE();

    _SEH2_TRY
    {
        DebugDirectory = RtlImageDirectoryEntryToData(BaseAddress,
                                                      TRUE,
                                                      IMAGE_DIRECTORY_ENTRY_DEBUG,
                                                      &DebugSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    return DebugDirectory;
}

static
NTSTATUS
MiAllocateSystemImage(
    _In_ ULONG Pages,
    _Out_ PVOID *ImageBase)
{
    ULONG Attempts = 0;
    NTSTATUS Status;
    ULONG64 Base;

    *ImageBase = NULL;

    Base = MiReserveSystemPtes(&MiSystem, Pages);
    if (Base == 0)
        return STATUS_INSUFFICIENT_RESOURCES;

    do
    {
        Status = MiSystemCommitPages(&MiSystem, Base, Pages, MI_PROT_EXECUTE_READWRITE);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (!NT_SUCCESS(Status))
    {
        MiReleaseSystemPtes(&MiSystem, Base, Pages);
        return Status;
    }

    *ImageBase = (PVOID)(ULONG_PTR)Base;
    return STATUS_SUCCESS;
}

static
BOOLEAN
MiIsLoadedSystemImage(
    _In_ PVOID ImageBase)
{
    PMI_SYSTEM_PTES Ptes = MiSystem.SystemPtes;

    return (BOOLEAN)(Ptes != NULL && (ULONG64)(ULONG_PTR)ImageBase >= Ptes->Base &&
                     (ULONG64)(ULONG_PTR)ImageBase < Ptes->Base + (Ptes->PageCount << PAGE_SHIFT));
}

static
VOID
MiFreeSystemImage(
    _In_ PVOID ImageBase,
    _In_ ULONG Pages)
{
    if (!MiIsLoadedSystemImage(ImageBase))
        return;

    MiSystemDecommitPages(&MiSystem, (ULONG64)(ULONG_PTR)ImageBase, Pages);
    MiReleaseSystemPtes(&MiSystem, (ULONG64)(ULONG_PTR)ImageBase, Pages);
}

NTSTATUS
NTAPI
MiLoadImageSection(_Inout_ PSECTION *SectionPtr,
                   _Out_ PVOID *ImageBase,
                   _In_ PUNICODE_STRING FileName,
                   _In_ BOOLEAN SessionLoad,
                   _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    SECTION_IMAGE_INFORMATION ImageInformation;
    PSECTION Section = *SectionPtr;
    PVOID View = NULL;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;
    ULONG Pages;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(LdrEntry);

    if (SessionLoad)
        return STATUS_NOT_IMPLEMENTED;

    Status = MmGetSectionImageInformation(Section, &ImageInformation);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MmMapViewInSystemSpace(Section, &View, &ViewSize);
    if (Status == STATUS_IMAGE_MACHINE_TYPE_MISMATCH)
        Status = STATUS_INVALID_IMAGE_FORMAT;

    if (!NT_SUCCESS(Status))
        return Status;

    Pages = (ULONG)(ROUND_TO_PAGES(ViewSize) >> PAGE_SHIFT);

    Status = MiAllocateSystemImage(Pages, ImageBase);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(*ImageBase, View, ViewSize);
        KeSweepICache(*ImageBase, ViewSize);
    }

    MmUnmapViewInSystemSpace(View);
    return Status;
}

#ifndef RVA
#define RVA(m, b) ((PVOID)((ULONG_PTR)(b) + (ULONG_PTR)(m)))
#endif

ULONG
NTAPI
NameToOrdinal(
    _In_ PCSTR ExportName,
    _In_ PVOID ImageBase,
    _In_ ULONG NumberOfNames,
    _In_ PULONG NameTable,
    _In_ PUSHORT OrdinalTable)
{
    LONG Low, Mid, High, Ret;

    if (!NumberOfNames)
        return MAXULONG;

    Low = Mid = 0;
    High = NumberOfNames - 1;
    while (High >= Low)
    {
        Mid = (Low + High) >> 1;

        Ret = strcmp(ExportName, (PCHAR)RVA(ImageBase, NameTable[Mid]));
        if (Ret < 0)
        {
            High = Mid - 1;
        }
        else if (Ret > 0)
        {
            Low = Mid + 1;
        }
        else
        {
            break;
        }
    }

    if (High < Low)
        return MAXULONG;

    return OrdinalTable[Mid];
}

NTSTATUS
NTAPI
RtlpFindExportedRoutineByName(
    _In_ PVOID ImageBase,
    _In_ PCSTR ExportName,
    _Out_ PVOID* Function,
    _Out_opt_ PBOOLEAN IsForwarder,
    _In_ NTSTATUS NotFoundStatus)
{
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PULONG NameTable;
    PUSHORT OrdinalTable;
    ULONG ExportSize;
    ULONG Ordinal;
    PULONG ExportTable;
    ULONG_PTR FunctionAddress;

    PAGED_CODE();

    ExportDirectory = RtlImageDirectoryEntryToData(ImageBase,
                                                   TRUE,
                                                   IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                   &ExportSize);
    if (!ExportDirectory)
        return STATUS_INVALID_PARAMETER;

    NameTable = (PULONG)RVA(ImageBase, ExportDirectory->AddressOfNames);
    OrdinalTable = (PUSHORT)RVA(ImageBase, ExportDirectory->AddressOfNameOrdinals);

    Ordinal = NameToOrdinal(ExportName,
                            ImageBase,
                            ExportDirectory->NumberOfNames,
                            NameTable,
                            OrdinalTable);

    if (Ordinal == MAXULONG)
        return NotFoundStatus;

    if (Ordinal >= ExportDirectory->NumberOfFunctions)
        return NotFoundStatus;

    ExportTable = (PULONG)RVA(ImageBase, ExportDirectory->AddressOfFunctions);
    FunctionAddress = (ULONG_PTR)RVA(ImageBase, ExportTable[Ordinal]);

    if (IsForwarder)
    {
        *IsForwarder = FALSE;
        if ((FunctionAddress > (ULONG_PTR)ExportDirectory) &&
            (FunctionAddress < (ULONG_PTR)ExportDirectory + ExportSize))
        {
            *IsForwarder = TRUE;
        }
    }

    *Function = (PVOID)FunctionAddress;
    return STATUS_SUCCESS;
}

PVOID
NTAPI
RtlFindExportedRoutineByName(
    _In_ PVOID ImageBase,
    _In_ PCSTR ExportName)
{
    NTSTATUS Status;
    BOOLEAN IsForwarder = FALSE;
    PVOID Function;

    PAGED_CODE();

    Status = RtlpFindExportedRoutineByName(ImageBase,
                                           ExportName,
                                           &Function,
                                           &IsForwarder,
                                           STATUS_ENTRYPOINT_NOT_FOUND);
    if (!NT_SUCCESS(Status))
        return NULL;

    if (IsForwarder)
    {
        DPRINT1("RtlFindExportedRoutineByName does not support forwarders!\n", FALSE);
        return NULL;
    }

    return Function;
}

NTSTATUS
NTAPI
MmCallDllInitialize(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PLIST_ENTRY ModuleListHead)
{
    UNICODE_STRING ServicesKeyName = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\");
    PMM_DLL_INITIALIZE DllInit;
    UNICODE_STRING RegPath, ImportName;
    PCWCH Extension;
    NTSTATUS Status;

    PAGED_CODE();

    DllInit = (PMM_DLL_INITIALIZE)
        RtlFindExportedRoutineByName(LdrEntry->DllBase, "DllInitialize");
    if (!DllInit)
        return STATUS_SUCCESS;

    ImportName.Length = LdrEntry->BaseDllName.Length;
    ImportName.MaximumLength = LdrEntry->BaseDllName.MaximumLength;
    ImportName.Buffer = LdrEntry->BaseDllName.Buffer;

    RegPath.MaximumLength = ServicesKeyName.Length +
        ImportName.Length + sizeof(UNICODE_NULL);
    RegPath.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                           RegPath.MaximumLength,
                                           TAG_LDR_WSTR);

    if (!RegPath.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RegPath.Length = ServicesKeyName.Length;
    RtlCopyMemory(RegPath.Buffer,
                  ServicesKeyName.Buffer,
                  ServicesKeyName.Length);

    Extension = wcschr(ImportName.Buffer, L'.');
    if (Extension)
        ImportName.Length = (USHORT)(Extension - ImportName.Buffer) * sizeof(WCHAR);

    RtlAppendUnicodeStringToString(&RegPath, &ImportName);

    DPRINT("Calling DllInit(%wZ)\n", &RegPath);
    Status = DllInit(&RegPath);

    ExFreePoolWithTag(RegPath.Buffer, TAG_LDR_WSTR);

    UNREFERENCED_PARAMETER(ModuleListHead);

    return Status;
}

BOOLEAN
MiCallDllUnloadAndUnloadDll(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    NTSTATUS Status;
    PMM_DLL_UNLOAD DllUnload;

    PAGED_CODE();

    DllUnload = (PMM_DLL_UNLOAD)
        RtlFindExportedRoutineByName(LdrEntry->DllBase, "DllUnload");
    if (!DllUnload)
        return FALSE;

    Status = DllUnload();
    if (!NT_SUCCESS(Status))
        return FALSE;

    ASSERT(LdrEntry->LoadCount == 0);
    LdrEntry->LoadCount = 1;

    MmUnloadSystemImage(LdrEntry);
    return TRUE;
}

NTSTATUS
NTAPI
MiDereferenceImports(IN PLOAD_IMPORTS ImportList)
{
    SIZE_T i;
    LOAD_IMPORTS SingleEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PVOID CurrentImports;
    PAGED_CODE();

    if ((ImportList == NULL) ||
        (ImportList == MM_SYSLDR_NO_IMPORTS) ||
        (ImportList == MM_SYSLDR_BOOT_LOADED))
    {
        return STATUS_SUCCESS;
    }

    if ((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY)
    {
        SingleEntry.Count = 1;
        SingleEntry.Entry[0] = (PVOID)((ULONG_PTR)ImportList &~ MM_SYSLDR_SINGLE_ENTRY);

        ImportList = &SingleEntry;
    }
    else if (ImportList->Count == 0)
    {
        return STATUS_SUCCESS;
    }

    for (i = 0; (i < ImportList->Count) && (ImportList->Entry[i]); i++)
    {
        LdrEntry = ImportList->Entry[i];
        DPRINT1("%wZ <%wZ>\n", &LdrEntry->FullDllName, &LdrEntry->BaseDllName);

        if (LdrEntry->LoadedImports == MM_SYSLDR_BOOT_LOADED) continue;

        ASSERT(LdrEntry->LoadCount >= 1);
        if (!--LdrEntry->LoadCount)
        {
            CurrentImports = LdrEntry->LoadedImports;

            LdrEntry->LoadedImports = MM_SYSLDR_NO_IMPORTS;
            if (MiCallDllUnloadAndUnloadDll(LdrEntry))
            {
                MiDereferenceImports(CurrentImports);

                if ((CurrentImports != MM_SYSLDR_BOOT_LOADED) &&
                    (CurrentImports != MM_SYSLDR_NO_IMPORTS) &&
                    !((ULONG_PTR)CurrentImports & MM_SYSLDR_SINGLE_ENTRY))
                {
                    ExFreePoolWithTag(CurrentImports, TAG_LDR_IMPORTS);
                }
            }
            else
            {
                LdrEntry->LoadedImports = CurrentImports;
            }
        }
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
MiClearImports(IN PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PAGED_CODE();

    if ((LdrEntry->LoadedImports == MM_SYSLDR_BOOT_LOADED) ||
        (LdrEntry->LoadedImports == MM_SYSLDR_NO_IMPORTS) ||
        ((ULONG_PTR)LdrEntry->LoadedImports & MM_SYSLDR_SINGLE_ENTRY))
    {
        return;
    }

    ExFreePoolWithTag(LdrEntry->LoadedImports, TAG_LDR_IMPORTS);
    LdrEntry->LoadedImports = MM_SYSLDR_BOOT_LOADED;
}

VOID
NTAPI
MiProcessLoaderEntry(IN PLDR_DATA_TABLE_ENTRY LdrEntry,
                     IN BOOLEAN Insert)
{
    KIRQL OldIrql;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PsLoadedModuleResource, TRUE);

    OldIrql = KeAcquireSpinLockRaiseToSynch(&PsLoadedModuleSpinLock);

    if (Insert)
        InsertTailList(&PsLoadedModuleList, &LdrEntry->InLoadOrderLinks);
    else
        RemoveEntryList(&LdrEntry->InLoadOrderLinks);

    KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);
    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();
}

CODE_SEG("INIT")
VOID
NTAPI
MiUpdateThunks(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
               IN PVOID OldBase,
               IN PVOID NewBase,
               IN ULONG Size)
{
    ULONG_PTR OldBaseTop, Delta;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY NextEntry;
    ULONG ImportSize;

#ifdef _WORKING_LINKER_
    ULONG i;
#endif
    PULONG_PTR ImageThunk;
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;

    OldBaseTop = (ULONG_PTR)OldBase + Size - 1;
    Delta = (ULONG_PTR)NewBase - (ULONG_PTR)OldBase;

    for (NextEntry = LoaderBlock->LoadOrderListHead.Flink;
         NextEntry != &LoaderBlock->LoadOrderListHead;
         NextEntry = NextEntry->Flink)
    {
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);
#ifdef _WORKING_LINKER_

        ImageThunk = RtlImageDirectoryEntryToData(LdrEntry->DllBase,
                                                  TRUE,
                                                  IMAGE_DIRECTORY_ENTRY_IAT,
                                                  &ImportSize);
        if (!ImageThunk) continue;

        DPRINT("[Mm0]: Updating thunks in: %wZ\n", &LdrEntry->BaseDllName);
        for (i = 0; i < ImportSize; i++, ImageThunk++)
        {
            if ((*ImageThunk >= (ULONG_PTR)OldBase) && (*ImageThunk <= OldBaseTop))
            {
                DPRINT("[Mm0]: Updating IAT at: %p. Old Entry: %p. New Entry: %p.\n",
                        ImageThunk, *ImageThunk, *ImageThunk + Delta);
                *ImageThunk += Delta;
            }
        }
#else

        ImportDescriptor = RtlImageDirectoryEntryToData(LdrEntry->DllBase,
                                                        TRUE,
                                                        IMAGE_DIRECTORY_ENTRY_IMPORT,
                                                        &ImportSize);
        if (!ImportDescriptor) continue;

        DPRINT("[Mm0]: Updating thunks in: %wZ\n", &LdrEntry->BaseDllName);
        while ((ImportDescriptor->Name) &&
               (ImportDescriptor->OriginalFirstThunk))
        {
            ImageThunk = (PVOID)((ULONG_PTR)LdrEntry->DllBase +
                                 ImportDescriptor->FirstThunk);
            while (*ImageThunk)
            {
                if ((*ImageThunk >= (ULONG_PTR)OldBase) && (*ImageThunk <= OldBaseTop))
                {
                    DPRINT("[Mm0]: Updating IAT at: %p. Old Entry: %p. New Entry: %p.\n",
                            ImageThunk, *ImageThunk, *ImageThunk + Delta);
                    *ImageThunk += Delta;
                }

                ImageThunk++;
            }

            ImportDescriptor++;
        }
#endif
    }
}

NTSTATUS
NTAPI
MiSnapThunk(IN PVOID DllBase,
            IN PVOID ImageBase,
            IN PIMAGE_THUNK_DATA Name,
            IN PIMAGE_THUNK_DATA Address,
            IN PIMAGE_EXPORT_DIRECTORY ExportDirectory,
            IN ULONG ExportSize,
            IN BOOLEAN SnapForwarder,
            OUT PCHAR *MissingApi)
{
    BOOLEAN IsOrdinal;
    ULONG Ordinal;
    PULONG NameTable;
    PUSHORT OrdinalTable;
    PIMAGE_IMPORT_BY_NAME NameImport;
    USHORT Hint;
    NTSTATUS Status;
    PCHAR MissingForwarder;
    CHAR NameBuffer[MAXIMUM_FILENAME_LENGTH];
    PULONG ExportTable;
    ANSI_STRING DllName;
    UNICODE_STRING ForwarderName;
    PLIST_ENTRY NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG ForwardExportSize;
    PIMAGE_EXPORT_DIRECTORY ForwardExportDirectory;
    PIMAGE_IMPORT_BY_NAME ForwardName;
    SIZE_T ForwardLength;
    IMAGE_THUNK_DATA ForwardThunk;

    PAGED_CODE();

    IsOrdinal = IMAGE_SNAP_BY_ORDINAL(Name->u1.Ordinal);
    if ((IsOrdinal) && !(SnapForwarder))
    {
        Ordinal = (USHORT)(IMAGE_ORDINAL(Name->u1.Ordinal) -
                           ExportDirectory->Base);
        *MissingApi = (PCHAR)(ULONG_PTR)Ordinal;
    }
    else
    {
        if (SnapForwarder)
        {
            NameImport = (PIMAGE_IMPORT_BY_NAME)Name->u1.AddressOfData;
        }
        else
        {
            NameImport = (PIMAGE_IMPORT_BY_NAME)((ULONG_PTR)ImageBase +
                                                 Name->u1.AddressOfData);
        }

        RtlStringCbCopyA(*MissingApi,
                         MAXIMUM_FILENAME_LENGTH,
                         (PCHAR)NameImport->Name);

        DPRINT("Import name: %s\n", NameImport->Name);
        NameTable = (PULONG)((ULONG_PTR)DllBase +
                             ExportDirectory->AddressOfNames);
        OrdinalTable = (PUSHORT)((ULONG_PTR)DllBase +
                                 ExportDirectory->AddressOfNameOrdinals);

        Hint = NameImport->Hint;
        if ((Hint < ExportDirectory->NumberOfNames) &&
            !(strcmp((PCHAR)NameImport->Name, (PCHAR)DllBase + NameTable[Hint])))
        {
            Ordinal = OrdinalTable[Hint];
        }
        else
        {
            Ordinal = NameToOrdinal((PCHAR)NameImport->Name,
                                    DllBase,
                                    ExportDirectory->NumberOfNames,
                                    NameTable,
                                    OrdinalTable);

            if (Ordinal == MAXULONG)
            {
                DPRINT1("Warning: Driver failed to load, %s not found\n", NameImport->Name);
                return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
            }
        }
    }

    if (Ordinal >= ExportDirectory->NumberOfFunctions)
    {
        Status = STATUS_DRIVER_ORDINAL_NOT_FOUND;
    }
    else
    {
        MissingForwarder = NameBuffer;

        ExportTable = (PULONG)((ULONG_PTR)DllBase +
                               ExportDirectory->AddressOfFunctions);
        Address->u1.Function = (ULONG_PTR)DllBase + ExportTable[Ordinal];

        Status = STATUS_SUCCESS;

        if ((Address->u1.Function > (ULONG_PTR)ExportDirectory) &&
            (Address->u1.Function < (ULONG_PTR)ExportDirectory + ExportSize))
        {
            Status = STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;

            DllName.Buffer = (PCHAR)Address->u1.Function;
            DllName.Length = (USHORT)(strchr(DllName.Buffer, '.') -
                                      DllName.Buffer) +
                                      sizeof(ANSI_NULL);
            DllName.MaximumLength = DllName.Length;

            if (!NT_SUCCESS(RtlAnsiStringToUnicodeString(&ForwarderName,
                                                         &DllName,
                                                         TRUE)))
            {
                return Status;
            }

            NextEntry = PsLoadedModuleList.Flink;
            while (NextEntry != &PsLoadedModuleList)
            {
                LdrEntry = CONTAINING_RECORD(NextEntry,
                                             LDR_DATA_TABLE_ENTRY,
                                             InLoadOrderLinks);

                if (RtlPrefixUnicodeString(&ForwarderName,
                                           &LdrEntry->BaseDllName,
                                           TRUE))
                {
                    ForwardExportDirectory =
                        RtlImageDirectoryEntryToData(LdrEntry->DllBase,
                                                     TRUE,
                                                     IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                     &ForwardExportSize);
                    if (!ForwardExportDirectory) break;

                    ForwardLength = strlen(DllName.Buffer + DllName.Length) +
                                    sizeof(ANSI_NULL);
                    ForwardName = ExAllocatePoolWithTag(PagedPool,
                                                        sizeof(*ForwardName) +
                                                        ForwardLength,
                                                        TAG_LDR_WSTR);
                    if (!ForwardName) break;

                    RtlCopyMemory(&ForwardName->Name[0],
                                  DllName.Buffer + DllName.Length,
                                  ForwardLength);
                    ForwardName->Hint = 0;

                    ForwardThunk.u1.AddressOfData = (ULONG_PTR)ForwardName;

                    Status = MiSnapThunk(LdrEntry->DllBase,
                                         ImageBase,
                                         &ForwardThunk,
                                         &ForwardThunk,
                                         ForwardExportDirectory,
                                         ForwardExportSize,
                                         TRUE,
                                         &MissingForwarder);

                    ExFreePoolWithTag(ForwardName, TAG_LDR_WSTR);
                    Address->u1 = ForwardThunk.u1;
                    break;
                }

                NextEntry = NextEntry->Flink;
            }

            RtlFreeUnicodeString(&ForwarderName);
        }
    }

    return Status;
}

NTSTATUS
NTAPI
MmUnloadSystemImage(IN PVOID ImageHandle)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry = ImageHandle;
    PVOID BaseAddress = LdrEntry->DllBase;
    NTSTATUS Status;
    STRING TempName;
    BOOLEAN HadEntry = FALSE;

    KeEnterCriticalRegion();
    KeWaitForSingleObject(&MmSystemLoadLock,
                          WrVirtualMemory,
                          KernelMode,
                          FALSE,
                          NULL);

    if (LdrEntry->LoadedImports == MM_SYSLDR_BOOT_LOADED) goto Done;

    ASSERT(LdrEntry->LoadCount != 0);
    LdrEntry->LoadCount--;

    if (LdrEntry->LoadCount) goto Done;

    if (LdrEntry->Flags & LDRP_DEBUG_SYMBOLS_LOADED)
    {
        Status = RtlUnicodeStringToAnsiString(&TempName,
                                              &LdrEntry->BaseDllName,
                                              TRUE);
        if (NT_SUCCESS(Status))
        {
            DbgUnLoadImageSymbols(&TempName,
                                  BaseAddress,
                                  (ULONG_PTR)PsGetCurrentProcessId());
            RtlFreeAnsiString(&TempName);
        }
    }

    MiFreeSystemImage(LdrEntry->DllBase, (ULONG)(ROUND_TO_PAGES(LdrEntry->SizeOfImage) >> PAGE_SHIFT));

    if (LdrEntry->InLoadOrderLinks.Flink)
    {
        MiProcessLoaderEntry(LdrEntry, FALSE);
        HadEntry = TRUE;
    }

    MiDereferenceImports(LdrEntry->LoadedImports);
    MiClearImports(LdrEntry);

    if (HadEntry)
    {
        if (LdrEntry->FullDllName.Buffer)
        {
            ExFreePoolWithTag(LdrEntry->FullDllName.Buffer, TAG_LDR_WSTR);
        }

        if (LdrEntry->SectionPointer)
        {
            ObDereferenceObject(LdrEntry->SectionPointer);
        }

        ExFreePoolWithTag(LdrEntry, TAG_MODULE_OBJECT);
    }

Done:
    KeReleaseMutant(&MmSystemLoadLock, MUTANT_INCREMENT, FALSE, FALSE);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MiResolveImageReferences(IN PVOID ImageBase,
                         IN PUNICODE_STRING ImageFileDirectory,
                         IN PUNICODE_STRING NamePrefix OPTIONAL,
                         OUT PCHAR *MissingApi,
                         OUT PWCHAR *MissingDriver,
                         OUT PLOAD_IMPORTS *LoadImports)
{
    static UNICODE_STRING DriversFolderName = RTL_CONSTANT_STRING(L"drivers\\");
    PCHAR MissingApiBuffer = *MissingApi, ImportName;
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor, CurrentImport;
    ULONG ImportSize, ImportCount = 0, LoadedImportsSize, ExportSize;
    PLOAD_IMPORTS LoadedImports, NewImports;
    ULONG i;
    BOOLEAN GdiLink, NormalLink;
    BOOLEAN GdiImport;
    BOOLEAN ReferenceNeeded, Loaded;
    ANSI_STRING TempString;
    UNICODE_STRING NameString, DllName;
    PLDR_DATA_TABLE_ENTRY LdrEntry = NULL, DllEntry, ImportEntry = NULL;
    PVOID ImportBase, DllBase;
    PLIST_ENTRY NextEntry;
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    NTSTATUS Status;
    PIMAGE_THUNK_DATA OrigThunk, FirstThunk;

    PAGED_CODE();

    DPRINT("%s - ImageBase: %p. ImageFileDirectory: %wZ\n",
           __FUNCTION__, ImageBase, ImageFileDirectory);

    NameString.Buffer = NULL;

    *LoadImports = MM_SYSLDR_NO_IMPORTS;

    ImportDescriptor = RtlImageDirectoryEntryToData(ImageBase,
                                                    TRUE,
                                                    IMAGE_DIRECTORY_ENTRY_IMPORT,
                                                    &ImportSize);
    if (!ImportDescriptor) return STATUS_SUCCESS;

    for (CurrentImport = ImportDescriptor;
         (CurrentImport->Name) && (CurrentImport->OriginalFirstThunk);
         CurrentImport++)
    {
        ImportCount++;
    }

    if (ImportCount)
    {
        LoadedImportsSize = ImportCount * sizeof(PVOID) + sizeof(SIZE_T);
        LoadedImports = ExAllocatePoolWithTag(PagedPool,
                                              LoadedImportsSize,
                                              TAG_LDR_IMPORTS);
        if (LoadedImports)
        {
            RtlZeroMemory(LoadedImports, LoadedImportsSize);
            LoadedImports->Count = ImportCount;
        }
    }
    else
    {
        LoadedImports = NULL;
    }

    GdiLink = NormalLink = FALSE;
    ImportCount = 0;
    while ((ImportDescriptor->Name) && (ImportDescriptor->OriginalFirstThunk))
    {
        ImportName = (PCHAR)((ULONG_PTR)ImageBase + ImportDescriptor->Name);

        GdiLink = GdiLink ||
                  !(_strnicmp(ImportName, "win32k", sizeof("win32k") - 1));

        GdiImport = !(_strnicmp(ImportName, "win32k", sizeof("win32k") - 1)) ||
                    !(_strnicmp(ImportName, "ntoskrnl", sizeof("ntoskrnl") - 1)) ||
                    !(_strnicmp(ImportName, "hal", sizeof("hal") - 1)) ||
                    !(_strnicmp(ImportName, "watchdog", sizeof("watchdog") - 1)) ||
                    !(_strnicmp(ImportName, "dxapi", sizeof("dxapi") - 1)) ||
                    !(_strnicmp(ImportName, "coverage", sizeof("coverage") - 1)) ||
                    !(_strnicmp(ImportName, "irt", sizeof("irt") - 1));
        NormalLink = NormalLink || !GdiImport;

        if (GdiLink && NormalLink)
        {
            Status = STATUS_PROCEDURE_NOT_FOUND;
            goto Failure;
        }

        if (!(_strnicmp(ImportName, "ntdll", sizeof("ntdll") - 1)) ||
            !(_strnicmp(ImportName, "winsrv", sizeof("winsrv") - 1)) ||
            !(_strnicmp(ImportName, "advapi32", sizeof("advapi32") - 1)) ||
            !(_strnicmp(ImportName, "kernel32", sizeof("kernel32") - 1)) ||
            !(_strnicmp(ImportName, "user32", sizeof("user32") - 1)) ||
            !(_strnicmp(ImportName, "gdi32", sizeof("gdi32") - 1)))
        {
            Status = STATUS_PROCEDURE_NOT_FOUND;
            goto Failure;
        }

        if (!(_strnicmp(ImportName, "ntoskrnl", sizeof("ntoskrnl") - 1)) ||
            !(_strnicmp(ImportName, "win32k", sizeof("win32k") - 1)) ||
            !(_strnicmp(ImportName, "hal", sizeof("hal") - 1)))
        {
            ReferenceNeeded = FALSE;
        }
        else
        {
            ReferenceNeeded = TRUE;
        }

        RtlInitAnsiString(&TempString, ImportName);
        Status = RtlAnsiStringToUnicodeString(&NameString, &TempString, TRUE);
        if (!NT_SUCCESS(Status))
        {
            goto Failure;
        }

        {
            static UNICODE_STRING ApiNtosPrefix = RTL_CONSTANT_STRING(L"api-ms-win-ntos-");
            static UNICODE_STRING ExtNtosPrefix = RTL_CONSTANT_STRING(L"ext-ms-win-ntos-");
            static UNICODE_STRING ApiWin32kPrefix = RTL_CONSTANT_STRING(L"api-ms-win-core-win32k-");
            static UNICODE_STRING ExtWin32kPrefix = RTL_CONSTANT_STRING(L"ext-ms-win-core-win32k-");
            UNICODE_STRING ApiSetHost;
            BOOLEAN ApiSetResolved = FALSE;

            if (RtlPrefixUnicodeString(&ApiNtosPrefix, &NameString, TRUE) ||
                RtlPrefixUnicodeString(&ExtNtosPrefix, &NameString, TRUE) ||
                RtlPrefixUnicodeString(&ApiWin32kPrefix, &NameString, TRUE) ||
                RtlPrefixUnicodeString(&ExtWin32kPrefix, &NameString, TRUE))
            {
                Status = ApiSetResolveToHost(APISET_WIN10, &NameString, &ApiSetResolved, &ApiSetHost);
                if (!NT_SUCCESS(Status))
                {
                    RtlFreeUnicodeString(&NameString);
                    NameString.Buffer = NULL;
                    goto Failure;
                }

                if (ApiSetResolved)
                {
                    if (!ApiSetHost.Length)
                    {
                        if (!_stricmp(ImportName,
                                     "ext-ms-win-ntos-ksr-l1-1-5.dll") ||
                            !_stricmp(ImportName,
                                     "ext-ms-win-ntos-ksr-l1-1-5"))
                        {
                            OrigThunk = (PVOID)((ULONG_PTR)ImageBase +
                                                ImportDescriptor->OriginalFirstThunk);
                            FirstThunk = (PVOID)((ULONG_PTR)ImageBase +
                                                 ImportDescriptor->FirstThunk);

                            while (OrigThunk->u1.AddressOfData)
                            {
                                PIMAGE_IMPORT_BY_NAME NameImport;
                                PVOID Routine;

                                if (IMAGE_SNAP_BY_ORDINAL(OrigThunk->u1.Ordinal))
                                {
                                    *MissingApi = (PCHAR)(ULONG_PTR)
                                                  IMAGE_ORDINAL(OrigThunk->u1.Ordinal);
                                    Status = STATUS_DRIVER_ORDINAL_NOT_FOUND;
                                    goto Failure;
                                }

                                NameImport = (PVOID)((ULONG_PTR)ImageBase +
                                                     OrigThunk->u1.AddressOfData);
                                RtlStringCbCopyA(*MissingApi,
                                                 MAXIMUM_FILENAME_LENGTH,
                                                 (PCHAR)NameImport->Name);
                                Routine = IopResolveKsrApiSetRoutine(
                                              (PCHAR)NameImport->Name);
                                if (Routine == NULL)
                                {
                                    Status = STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
                                    goto Failure;
                                }

                                FirstThunk->u1.Function = (ULONG_PTR)Routine;
                                OrigThunk++;
                                FirstThunk++;
                                *MissingApi = MissingApiBuffer;
                            }

                            RtlFreeUnicodeString(&NameString);
                            NameString.Buffer = NULL;
                            ImportDescriptor++;
                            continue;
                        }

                        Status = STATUS_PROCEDURE_NOT_FOUND;
                        goto Failure;
                    }
                    else
                    {
                        RtlFreeUnicodeString(&NameString);
                        NameString.Buffer = NULL;
                        Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE, &ApiSetHost, &NameString);
                        if (!NT_SUCCESS(Status))
                            goto Failure;

                        DPRINT("Resolved kernel API set '%s' to '%wZ'\n", ImportName, &NameString);
                    }
                }
            }
        }

        if (NamePrefix) DPRINT1("Name Prefix not yet supported!\n");

CheckDllState:
        Loaded = FALSE;
        ImportBase = NULL;

        NextEntry = PsLoadedModuleList.Flink;
        while (NextEntry != &PsLoadedModuleList)
        {
            LdrEntry = CONTAINING_RECORD(NextEntry,
                                         LDR_DATA_TABLE_ENTRY,
                                         InLoadOrderLinks);
            if (RtlEqualUnicodeString(&NameString,
                                      &LdrEntry->BaseDllName,
                                      TRUE))
            {
                ImportBase = LdrEntry->DllBase;

                if (!(Loaded) && (ReferenceNeeded))
                {
                    if (!(LdrEntry->Flags & LDRP_LOAD_IN_PROGRESS))
                    {
                        LdrEntry->LoadCount++;
                    }
                }

                break;
            }

            NextEntry = NextEntry->Flink;
        }

        if (!ImportBase)
        {
            DllName.MaximumLength = NameString.Length +
                                    ImageFileDirectory->Length +
                                    sizeof(UNICODE_NULL);
            DllName.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                   DllName.MaximumLength,
                                                   TAG_LDR_WSTR);
            if (!DllName.Buffer)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Failure;
            }

            RtlCopyUnicodeString(&DllName, ImageFileDirectory);
            RtlAppendUnicodeStringToString(&DllName,
                                           &NameString);

            Status = MmLoadSystemImage(&DllName,
                                       NamePrefix,
                                       NULL,
                                       FALSE,
                                       (PVOID *)&DllEntry,
                                       &DllBase);

            if ((Status == STATUS_OBJECT_NAME_NOT_FOUND) &&
                TRUE)
            {
                ExFreePoolWithTag(DllName.Buffer, TAG_LDR_WSTR);

                DllName.MaximumLength += DriversFolderName.Length;

                DllName.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                       DllName.MaximumLength,
                                                       TAG_LDR_WSTR);
                if (!DllName.Buffer)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Failure;
                }

                RtlCopyUnicodeString(&DllName, ImageFileDirectory);
                RtlAppendUnicodeStringToString(&DllName, &DriversFolderName);

                RtlAppendUnicodeStringToString(&DllName, &NameString);

                Status = MmLoadSystemImage(&DllName,
                                           NamePrefix,
                                           NULL,
                                           FALSE,
                                           (PVOID *)&DllEntry,
                                           &DllBase);
            }

            if (!NT_SUCCESS(Status))
            {
                *MissingDriver = DllName.Buffer;
                *(PULONG)MissingDriver |= 1;
                *MissingApi = NULL;

                DPRINT1("Failed to load dependency: %wZ\n", &DllName);

                DllName.Buffer = NULL;

                goto Failure;
            }

            ExFreePoolWithTag(DllName.Buffer, TAG_LDR_WSTR);
            DllName.Buffer = NULL;

            Loaded = TRUE;

            ASSERT(DllBase == DllEntry->DllBase);

            Status = MmCallDllInitialize(DllEntry, &PsLoadedModuleList);
            if (!NT_SUCCESS(Status))
            {
                MmUnloadSystemImage(DllEntry);
                ERROR_DBGBREAK("MmCallDllInitialize failed with status 0x%x\n", Status);
                Loaded = FALSE;
            }

            goto CheckDllState;
        }

        if ((ReferenceNeeded) && (LoadedImports))
        {
            if (!(LdrEntry->Flags & LDRP_LOAD_IN_PROGRESS))
            {
                LoadedImports->Entry[ImportCount] = LdrEntry;
                ImportCount++;
            }
        }

        RtlFreeUnicodeString(&NameString);

        *MissingDriver = LdrEntry->BaseDllName.Buffer;
        ExportDirectory = RtlImageDirectoryEntryToData(ImportBase,
                                                       TRUE,
                                                       IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                       &ExportSize);
        if (!ExportDirectory)
        {
            DPRINT1("Warning: Driver failed to load, %S not found\n", *MissingDriver);
            Status = STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
            goto Failure;
        }

        if (ImportDescriptor->OriginalFirstThunk)
        {
            OrigThunk = (PVOID)((ULONG_PTR)ImageBase +
                                ImportDescriptor->OriginalFirstThunk);
            FirstThunk = (PVOID)((ULONG_PTR)ImageBase +
                                 ImportDescriptor->FirstThunk);

            while (OrigThunk->u1.AddressOfData)
            {
                Status = MiSnapThunk(ImportBase,
                                     ImageBase,
                                     OrigThunk++,
                                     FirstThunk++,
                                     ExportDirectory,
                                     ExportSize,
                                     FALSE,
                                     MissingApi);
                if (!NT_SUCCESS(Status))
                {
                    goto Failure;
                }

                *MissingApi = MissingApiBuffer;
            }
        }

        ImportDescriptor++;
    }

    if (LoadedImports)
    {
        ImportCount = 0;
        for (i = 0; i < LoadedImports->Count; i++)
        {
            if (LoadedImports->Entry[i])
            {
                ImportEntry = (PVOID)((ULONG_PTR)LoadedImports->Entry[i] |
                                      MM_SYSLDR_SINGLE_ENTRY);
                ImportCount++;
            }
        }

        if (!ImportCount)
        {
            ExFreePoolWithTag(LoadedImports, TAG_LDR_IMPORTS);
            LoadedImports = MM_SYSLDR_NO_IMPORTS;
        }
        else if (ImportCount == 1)
        {
            ExFreePoolWithTag(LoadedImports, TAG_LDR_IMPORTS);
            LoadedImports = (PLOAD_IMPORTS)ImportEntry;
        }
        else if (ImportCount != LoadedImports->Count)
        {
            LoadedImportsSize = ImportCount * sizeof(PVOID) + sizeof(SIZE_T);
            NewImports = ExAllocatePoolWithTag(PagedPool,
                                               LoadedImportsSize,
                                               TAG_LDR_IMPORTS);
            if (NewImports)
            {
                NewImports->Count = 0;

                for (i = 0; i < LoadedImports->Count; i++)
                {
                    if (LoadedImports->Entry[i])
                    {
                        NewImports->Entry[NewImports->Count] = LoadedImports->Entry[i];
                        NewImports->Count++;
                    }
                }

                ExFreePoolWithTag(LoadedImports, TAG_LDR_IMPORTS);
                LoadedImports = NewImports;
            }
        }

        *LoadImports = LoadedImports;
    }

    return STATUS_SUCCESS;

Failure:

    RtlFreeUnicodeString(&NameString);

    if (LoadedImports)
    {
        MiDereferenceImports(LoadedImports);
        ExFreePoolWithTag(LoadedImports, TAG_LDR_IMPORTS);
    }

    return Status;
}

VOID
NTAPI
MiFreeInitializationCode(IN PVOID InitStart,
                         IN PVOID InitEnd)
{
    ULONG64 Start = ROUND_TO_PAGES((ULONG64)(ULONG_PTR)InitStart);
    ULONG64 End = (ULONG64)(ULONG_PTR)InitEnd & ~((ULONG64)PAGE_SIZE - 1);

    if (End <= Start || !MiIsLoadedSystemImage(InitStart))
        return;

    MiSystemDecommitPages(&MiSystem, Start, (ULONG)((End - Start) >> PAGE_SHIFT));
}

CODE_SEG("INIT")
VOID
NTAPI
MiFindInitializationCode(OUT PVOID *StartVa,
                         OUT PVOID *EndVa)
{
    ULONG Size, SectionCount, Alignment;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR DllBase, InitStart, InitEnd, ImageEnd, InitCode;
    PLIST_ENTRY NextEntry;
    PIMAGE_NT_HEADERS NtHeader;
    PIMAGE_SECTION_HEADER Section, LastSection, InitSection;
    BOOLEAN InitFound;
    DBG_UNREFERENCED_LOCAL_VARIABLE(InitSection);

    InitCode = (ULONG_PTR)&MiFindInitializationCode;

    *StartVa = NULL;

    KeEnterCriticalRegion();
    KeWaitForSingleObject(&MmSystemLoadLock,
                          WrVirtualMemory,
                          KernelMode,
                          FALSE,
                          NULL);
    ExAcquireResourceExclusiveLite(&PsLoadedModuleResource, TRUE);

    NextEntry = PsLoadedModuleList.Flink;
    while (NextEntry != &PsLoadedModuleList)
    {
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        DllBase = (ULONG_PTR)LdrEntry->DllBase;

        if (LdrEntry->Flags & LDRP_MM_LOADED)
        {
            NextEntry = NextEntry->Flink;
            continue;
        }

        NtHeader = RtlImageNtHeader((PVOID)DllBase);
        if (!NtHeader)
        {
            NextEntry = NextEntry->Flink;
            continue;
        }

        Section = IMAGE_FIRST_SECTION(NtHeader);
        SectionCount = NtHeader->FileHeader.NumberOfSections;
        InitStart = 0;
        while (SectionCount > 0)
        {
            InitFound = FALSE;

            if ((strncmp((PCCH)Section->Name, "INIT", 5) == 0) ||
                ((Section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE)))
            {
                InitFound = TRUE;
                InitSection = Section;
            }

            if (InitFound)
            {
                Size = max(Section->SizeOfRawData, Section->Misc.VirtualSize);

                Alignment = NtHeader->OptionalHeader.SectionAlignment;

                InitStart = DllBase + Section->VirtualAddress;
                InitEnd = ALIGN_UP_BY(InitStart + Size, Alignment);

                InitStart = ALIGN_UP_BY(InitStart, PAGE_SIZE);
                InitEnd = ALIGN_DOWN_BY(InitEnd, PAGE_SIZE);

                if (SectionCount == 1)
                {
                    LastSection = Section;
                }
                else
                {
                    LastSection = NULL;
                    do
                    {
                        SectionCount--;
                        Section++;
                        if (Section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE)
                        {
                            LastSection = Section;
                        }
                        else
                        {
                            break;
                        }
                    }
                    while (SectionCount > 1);
                }

                if (LastSection)
                {
                    Size = max(LastSection->SizeOfRawData, LastSection->Misc.VirtualSize);

                    InitEnd = DllBase + LastSection->VirtualAddress + Size;

                    if (SectionCount != 1)
                    {
                        InitEnd = ALIGN_UP_BY(InitEnd, Alignment);
                        InitEnd = ALIGN_DOWN_BY(InitEnd, PAGE_SIZE);
                    }
                }

                ImageEnd = DllBase + LdrEntry->SizeOfImage;
                if (InitEnd > ImageEnd) InitEnd = ALIGN_UP_BY(ImageEnd, PAGE_SIZE);

                if (InitStart < InitEnd)
                {
                    if ((InitCode >= InitStart) && (InitCode < InitEnd))
                    {
                        ASSERT(*StartVa == 0);
                        *StartVa = (PVOID)InitStart;
                        *EndVa = (PVOID)InitEnd;
                    }
                    else
                    {
                        DPRINT("Freeing init code: %p-%p ('%wZ' @%p : '%s')\n",
                               (PVOID)InitStart,
                               (PVOID)InitEnd,
                               &LdrEntry->BaseDllName,
                               LdrEntry->DllBase,
                               InitSection->Name);
                        MiFreeInitializationCode((PVOID)InitStart, (PVOID)InitEnd);
                    }
                }
            }

            SectionCount--;
            Section++;
        }

        NextEntry = NextEntry->Flink;
    }

    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeReleaseMutant(&MmSystemLoadLock, MUTANT_INCREMENT, FALSE, FALSE);
    KeLeaveCriticalRegion();
}

VOID
NTAPI
MmFreeDriverInitialization(IN PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PIMAGE_SECTION_HEADER Section, DiscardSection = NULL;
    PVOID DllBase = LdrEntry->DllBase;
    PIMAGE_NT_HEADERS NtHeader;
    ULONG64 Start, End;
    ULONG i;

    NtHeader = RtlImageNtHeader(DllBase);
    if (!NtHeader || !MiIsLoadedSystemImage(DllBase))
        return;

    Section = IMAGE_FIRST_SECTION(NtHeader) + NtHeader->FileHeader.NumberOfSections;
    for (i = 0; i < NtHeader->FileHeader.NumberOfSections; i++)
    {
        Section--;
        if (!(Section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE))
            break;

        DiscardSection = Section;
    }

    if (!DiscardSection)
        return;

    Start = ROUND_TO_PAGES((ULONG64)(ULONG_PTR)DllBase + DiscardSection->VirtualAddress);
    End = ((ULONG64)(ULONG_PTR)DllBase + LdrEntry->SizeOfImage) & ~((ULONG64)PAGE_SIZE - 1);

    if (End > Start)
        MiSystemDecommitPages(&MiSystem, Start, (ULONG)((End - Start) >> PAGE_SHIFT));
}

VOID
NTAPI
MiReloadBootLoadedDrivers(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
MiBuildImportsForBootDrivers(VOID)
{
    PLIST_ENTRY NextEntry, NextEntry2;
    PLDR_DATA_TABLE_ENTRY LdrEntry, KernelEntry, HalEntry, LdrEntry2, LastEntry;
    PLDR_DATA_TABLE_ENTRY* EntryArray;
    UNICODE_STRING KernelName = RTL_CONSTANT_STRING(L"ntoskrnl.exe");
    UNICODE_STRING HalName = RTL_CONSTANT_STRING(L"hal.dll");
    PLOAD_IMPORTS LoadedImports;
    ULONG LoadedImportsSize, ImportSize;
    PULONG_PTR ImageThunk;
    ULONG_PTR DllBase, DllEnd;
    ULONG Modules = 0, i, j = 0;
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;

    KernelEntry = HalEntry = LastEntry = NULL;

    NextEntry = PsLoadedModuleList.Flink;
    while (NextEntry != &PsLoadedModuleList)
    {
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if (RtlEqualUnicodeString(&KernelName, &LdrEntry->BaseDllName, TRUE))
        {
            KernelEntry = LdrEntry;
        }
        else if (RtlEqualUnicodeString(&HalName, &LdrEntry->BaseDllName, TRUE))
        {
            HalEntry = LdrEntry;
        }

        if (LdrEntry->Flags & LDRP_DRIVER_DEPENDENT_DLL)
        {
            if ((LdrEntry == HalEntry) || (LdrEntry == KernelEntry))
            {
                LdrEntry->LoadCount = 1;
            }
            else
            {
                LdrEntry->LoadCount = 0;
            }
        }
        else
        {
            LdrEntry->LoadCount = 1;
        }

        LdrEntry->LoadedImports = MM_SYSLDR_BOOT_LOADED;

        NextEntry = NextEntry->Flink;
        Modules++;
    }

    if (!(HalEntry) || (!KernelEntry)) return STATUS_NOT_FOUND;

    EntryArray = ExAllocatePoolWithTag(PagedPool, Modules * sizeof(PVOID), TAG_LDR_IMPORTS);
    if (!EntryArray) return STATUS_INSUFFICIENT_RESOURCES;

    NextEntry = PsLoadedModuleList.Flink;
    while (NextEntry != &PsLoadedModuleList)
    {
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);
#ifdef _WORKING_LOADER_

        ImageThunk = RtlImageDirectoryEntryToData(LdrEntry->DllBase,
                                                  TRUE,
                                                  IMAGE_DIRECTORY_ENTRY_IAT,
                                                  &ImportSize);
        if (!ImageThunk)
#else

        ImportDescriptor = RtlImageDirectoryEntryToData(LdrEntry->DllBase,
                                                        TRUE,
                                                        IMAGE_DIRECTORY_ENTRY_IMPORT,
                                                        &ImportSize);
        if (!ImportDescriptor)
#endif
        {
            LdrEntry->LoadedImports = MM_SYSLDR_NO_IMPORTS;
            NextEntry = NextEntry->Flink;
            continue;
        }

        RtlZeroMemory(EntryArray, Modules * sizeof(PVOID));
#ifdef _WORKING_LOADER_
        ImportSize /= sizeof(ULONG_PTR);

        for (i = 0, DllBase = 0, DllEnd = 0; i < ImportSize; i++, ImageThunk++)
#else
        DllBase = DllEnd = i = 0;
        while ((ImportDescriptor->Name) &&
               (ImportDescriptor->OriginalFirstThunk))
        {
            ImageThunk = (PVOID)((ULONG_PTR)LdrEntry->DllBase +
                                 ImportDescriptor->FirstThunk);
            while (*ImageThunk)
#endif
            {
            if (DllBase)
            {
                if ((*ImageThunk >= DllBase) && (*ImageThunk < DllEnd))
                {
                    ASSERT(EntryArray[j]);
                    ImageThunk++;
                    continue;
                }
            }

            j = 0;
            NextEntry2 = PsLoadedModuleList.Flink;
            while (NextEntry2 != &PsLoadedModuleList)
            {
                LdrEntry2 = CONTAINING_RECORD(NextEntry2,
                                              LDR_DATA_TABLE_ENTRY,
                                              InLoadOrderLinks);

                DllBase = (ULONG_PTR)LdrEntry2->DllBase;
                DllEnd = DllBase + LdrEntry2->SizeOfImage;

                if ((*ImageThunk >= DllBase) && (*ImageThunk < DllEnd))
                {
                    EntryArray[j] = LdrEntry2;
                    break;
                }

                NextEntry2 = NextEntry2->Flink;
                j++;
            }

            if ((*ImageThunk < DllBase) || (*ImageThunk >= DllEnd))
            {
                if (*ImageThunk)
                {
                    ERROR_FATAL("Broken IAT entry for %p at %p (%lx)\n",
                                LdrEntry, ImageThunk, *ImageThunk);
                }

                DllBase = 0;
            }
#ifndef _WORKING_LOADER_
            ImageThunk++;
            }

            i++;
            ImportDescriptor++;
#endif
        }

        for (i = 0, ImportSize = 0; i < Modules; i++)
        {
            if ((EntryArray[i]) &&
                (EntryArray[i] != HalEntry) &&
                (EntryArray[i] != KernelEntry))
            {
                LastEntry = EntryArray[i];
                ImportSize++;
            }
        }

        if (!ImportSize)
        {
            LdrEntry->LoadedImports = MM_SYSLDR_NO_IMPORTS;
        }
        else if (ImportSize == 1)
        {
            LdrEntry->LoadedImports = (PVOID)((ULONG_PTR)LastEntry | MM_SYSLDR_SINGLE_ENTRY);
            LastEntry->LoadCount++;
        }
        else
        {
            LoadedImportsSize = ImportSize * sizeof(PVOID) + sizeof(SIZE_T);
            LoadedImports = ExAllocatePoolWithTag(PagedPool,
                                                  LoadedImportsSize,
                                                  TAG_LDR_IMPORTS);
            ASSERT(LoadedImports);

            LoadedImports->Count = ImportSize;

            for (i = 0, j = 0; i < Modules; i++)
            {
                if ((EntryArray[i]) &&
                    (EntryArray[i] != HalEntry) &&
                    (EntryArray[i] != KernelEntry))
                {
                    LoadedImports->Entry[j] = EntryArray[i];
                    EntryArray[i]->LoadCount++;
                    j++;
                }
            }

            ASSERT(j == ImportSize);
            LdrEntry->LoadedImports = LoadedImports;
        }

        NextEntry = NextEntry->Flink;
    }

    ExFreePoolWithTag(EntryArray, TAG_LDR_IMPORTS);

    KernelEntry->LoadedImports = MM_SYSLDR_BOOT_LOADED;
    HalEntry->LoadedImports = MM_SYSLDR_BOOT_LOADED;

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
MiLocateKernelSections(IN PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    ULONG_PTR DllBase;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER SectionHeader;
    ULONG Sections, Size;

    DllBase = (ULONG_PTR)LdrEntry->DllBase;
    NtHeaders = RtlImageNtHeader((PVOID)DllBase);
    SectionHeader = IMAGE_FIRST_SECTION(NtHeaders);

    for (Sections = NtHeaders->FileHeader.NumberOfSections;
         Sections > 0; --Sections, ++SectionHeader)
    {
        Size = max(SectionHeader->SizeOfRawData, SectionHeader->Misc.VirtualSize);

        if (*(PULONG)SectionHeader->Name == 'rsr.')
        {
            MiKernelResourceStart = DllBase + SectionHeader->VirtualAddress;
            MiKernelResourceEnd = ROUND_TO_PAGES(DllBase + SectionHeader->VirtualAddress + Size);
        }
        else if (*(PULONG)SectionHeader->Name == 'LOOP')
        {
            if (*(PULONG)&SectionHeader->Name[4] == 'EDOC')
            {
                ExPoolCodeStart = DllBase + SectionHeader->VirtualAddress;
                ExPoolCodeEnd = ExPoolCodeStart + Size;
            }
            else if (*(PUSHORT)&SectionHeader->Name[4] == 'MI')
            {
                MmPoolCodeStart = DllBase + SectionHeader->VirtualAddress;
                MmPoolCodeEnd = MmPoolCodeStart + Size;
            }
        }
        else if ((*(PULONG)SectionHeader->Name == 'YSIM') &&
                 (*(PULONG)&SectionHeader->Name[4] == 'ETPS'))
        {
            MmPteCodeStart = DllBase + SectionHeader->VirtualAddress;
            MmPteCodeEnd = MmPteCodeStart + Size;
        }
    }
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
MiInitializeLoadedModuleList(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry, NewEntry;
    PLIST_ENTRY ListHead, NextEntry;
    ULONG EntrySize;

    ExInitializeResourceLite(&PsLoadedModuleResource);
    KeInitializeSpinLock(&PsLoadedModuleSpinLock);
    InitializeListHead(&PsLoadedModuleList);

    ListHead = &LoaderBlock->LoadOrderListHead;
    NextEntry = ListHead->Flink;
    LdrEntry = CONTAINING_RECORD(NextEntry,
                                 LDR_DATA_TABLE_ENTRY,
                                 InLoadOrderLinks);
    PsNtosImageBase = (ULONG_PTR)LdrEntry->DllBase;

    MiLocateKernelSections(LdrEntry);

    while (NextEntry != ListHead)
    {
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if (!RtlImageNtHeader(LdrEntry->DllBase))
        {
            NextEntry = NextEntry->Flink;
            continue;
        }

        EntrySize = sizeof(LDR_DATA_TABLE_ENTRY) +
                    LdrEntry->BaseDllName.MaximumLength +
                    sizeof(UNICODE_NULL);
        NewEntry = ExAllocatePoolWithTag(NonPagedPool, EntrySize, TAG_MODULE_OBJECT);
        if (!NewEntry) return FALSE;

        *NewEntry = *LdrEntry;

        NewEntry->FullDllName.Buffer =
            ExAllocatePoolWithTag(PagedPool,
                                  LdrEntry->FullDllName.MaximumLength +
                                      sizeof(UNICODE_NULL),
                                  TAG_LDR_WSTR);
        if (!NewEntry->FullDllName.Buffer)
        {
            ExFreePoolWithTag(NewEntry, TAG_MODULE_OBJECT);
            return FALSE;
        }

        NewEntry->BaseDllName.Buffer = (PVOID)(NewEntry + 1);

        RtlCopyMemory(NewEntry->FullDllName.Buffer,
                      LdrEntry->FullDllName.Buffer,
                      LdrEntry->FullDllName.MaximumLength);
        RtlCopyMemory(NewEntry->BaseDllName.Buffer,
                      LdrEntry->BaseDllName.Buffer,
                      LdrEntry->BaseDllName.MaximumLength);

        NewEntry->BaseDllName.Buffer[NewEntry->BaseDllName.Length /
                                     sizeof(WCHAR)] = UNICODE_NULL;

        InsertTailList(&PsLoadedModuleList, &NewEntry->InLoadOrderLinks);
        NextEntry = NextEntry->Flink;
    }

    MiBuildImportsForBootDrivers();

    return TRUE;
}

BOOLEAN
NTAPI
MmChangeKernelResourceSectionProtection(IN ULONG_PTR ProtectionMask)
{
    if (MiKernelResourceStart == 0 || MiKernelResourceEnd <= MiKernelResourceStart)
        return FALSE;

    return (BOOLEAN)NT_SUCCESS(MiSystemProtect(&MiSystem, MiKernelResourceStart,
                                               (ULONG)((MiKernelResourceEnd - MiKernelResourceStart) >> PAGE_SHIFT),
                                               (ProtectionMask == MI_PROT_READONLY) ? MI_PROT_READONLY
                                                                               : MI_PROT_READWRITE));
}

VOID
NTAPI
MmMakeKernelResourceSectionWritable(VOID)
{
    if (MmChangeKernelResourceSectionProtection(MI_PROT_READWRITE))
    {
        MiKernelResourceStart = 0;
        MiKernelResourceEnd = 0;
    }
}

BOOLEAN
NTAPI
MiUseLargeDriverPage(IN ULONG NumberOfPtes,
                     IN OUT PVOID *ImageBaseAddress,
                     IN PUNICODE_STRING BaseImageName,
                     IN BOOLEAN BootDriver)
{
    UNREFERENCED_PARAMETER(NumberOfPtes);
    UNREFERENCED_PARAMETER(ImageBaseAddress);
    UNREFERENCED_PARAMETER(BaseImageName);
    UNREFERENCED_PARAMETER(BootDriver);
    return FALSE;
}


VOID
NTAPI
MiWriteProtectSystemImage(
    _In_ PVOID ImageBase)
{
    PIMAGE_SECTION_HEADER Section;
    PIMAGE_NT_HEADERS NtHeaders;
    PUCHAR PageProtection;
    ULONG Pages, Page, i;

    if (!MmEnforceWriteProtection || !MiIsLoadedSystemImage(ImageBase))
        return;

    NtHeaders = RtlImageNtHeader(ImageBase);
    if (NtHeaders == NULL)
        return;

    if (NtHeaders->OptionalHeader.MajorOperatingSystemVersion < 5 ||
        NtHeaders->OptionalHeader.MajorSubsystemVersion < 5 ||
        NtHeaders->OptionalHeader.SectionAlignment < PAGE_SIZE)
    {
        return;
    }

    Pages = (ULONG)(ROUND_TO_PAGES(NtHeaders->OptionalHeader.SizeOfImage) >> PAGE_SHIFT);
    PageProtection = ExAllocatePoolWithTag(NonPagedPool, Pages, 'pImM');
    if (PageProtection == NULL)
        return;

    RtlZeroMemory(PageProtection, Pages);
    Section = IMAGE_FIRST_SECTION(NtHeaders);

    for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++, Section++)
    {
        ULONG Size = max(Section->SizeOfRawData, Section->Misc.VirtualSize);
        ULONG First = Section->VirtualAddress >> PAGE_SHIFT;
        ULONG Last = (Section->VirtualAddress + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
        UCHAR Flags = 1;

        if (Section->Characteristics & IMAGE_SCN_MEM_WRITE)
            Flags |= 2;

        if (Section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            Flags |= 4;

        for (Page = First; Page < Last && Page < Pages; Page++)
            PageProtection[Page] |= Flags;
    }

    for (Page = 0; Page < Pages; )
    {
        ULONG Run = Page;
        UCHAR Flags = PageProtection[Page];
        ULONG Protection;

        while (Run < Pages && PageProtection[Run] == Flags)
            Run++;

        if (Flags & 2)
            Protection = (Flags & 4) ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE;
        else
            Protection = (Flags & 4) ? MI_PROT_EXECUTE_READ : MI_PROT_READONLY;

        MiSystemProtect(&MiSystem, (ULONG64)(ULONG_PTR)ImageBase + ((ULONG64)Page << PAGE_SHIFT), Run - Page,
                        Protection);
        Page = Run;
    }

    ExFreePoolWithTag(PageProtection, 'pImM');
    KeSweepICache(ImageBase, (SIZE_T)Pages << PAGE_SHIFT);
}


VOID
NTAPI
MiEnablePagingOfDriver(IN PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    UNREFERENCED_PARAMETER(LdrEntry);
}

#ifdef CONFIG_SMP
FORCEINLINE
BOOLEAN
MiVerifyImageIsOkForMpUse(
    _In_ PIMAGE_NT_HEADERS NtHeaders)
{
    if ((KeNumberProcessors > 1) &&
        (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_UP_SYSTEM_ONLY))
    {
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
NTAPI
MmVerifyImageIsOkForMpUse(
    _In_ PVOID BaseAddress)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PAGED_CODE();

    NtHeaders = RtlImageNtHeader(BaseAddress);
    if (!NtHeaders)
        return TRUE;
    return MiVerifyImageIsOkForMpUse(NtHeaders);
}
#endif

NTSTATUS
NTAPI
MmCheckSystemImage(
    _In_ HANDLE ImageHandle)
{
    NTSTATUS Status;
    HANDLE SectionHandle;
    PVOID ViewBase = NULL;
    SIZE_T ViewSize = 0;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_STANDARD_INFORMATION FileStandardInfo;
    KAPC_STATE ApcState;
    PIMAGE_NT_HEADERS NtHeaders;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateSection(&SectionHandle,
                             SECTION_MAP_EXECUTE,
                             &ObjectAttributes,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             ImageHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ZwCreateSection failed with status 0x%x\n", Status);
        return Status;
    }

    KeStackAttachProcess(&PsInitialSystemProcess->Pcb, &ApcState);

    Status = ZwMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                &ViewBase,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_EXECUTE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ZwMapViewOfSection failed with status 0x%x\n", Status);
        KeUnstackDetachProcess(&ApcState);
        ZwClose(SectionHandle);
        return Status;
    }

    Status = ZwQueryInformationFile(ImageHandle,
                                    &IoStatusBlock,
                                    &FileStandardInfo,
                                    sizeof(FileStandardInfo),
                                    FileStandardInformation);
    if (NT_SUCCESS(Status))
    {
        if (!LdrVerifyMappedImageMatchesChecksum(ViewBase,
                                                 ViewSize,
                                                 FileStandardInfo.
                                                 EndOfFile.LowPart))
        {
            Status = STATUS_IMAGE_CHECKSUM_MISMATCH;
            goto Fail;
        }

        NtHeaders = RtlImageNtHeader(ViewBase);
        if (!NtHeaders)
        {
            Status = STATUS_IMAGE_CHECKSUM_MISMATCH;
            goto Fail;
        }

        if ((NtHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_NATIVE) ||
            (NtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC))
        {
            Status = STATUS_INVALID_IMAGE_PROTECT;
            goto Fail;
        }

#ifdef CONFIG_SMP

        if (!MiVerifyImageIsOkForMpUse(NtHeaders))
        {
            Status = STATUS_IMAGE_MP_UP_MISMATCH;
        }
#endif
    }

Fail:
    ZwUnmapViewOfSection(NtCurrentProcess(), ViewBase);
    KeUnstackDetachProcess(&ApcState);
    ZwClose(SectionHandle);
    return Status;
}

PVOID
NTAPI
LdrpFetchAddressOfSecurityCookie(PVOID BaseAddress, ULONG SizeOfImage)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY ConfigDir;
    ULONG DirSize;
    PULONG_PTR Cookie = NULL;

    ConfigDir = RtlImageDirectoryEntryToData(BaseAddress,
                                             TRUE,
                                             IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG,
                                             &DirSize);

    if (!ConfigDir ||
        DirSize < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, SecurityCookie))
    {
        return NULL;
    }

    Cookie = (PULONG_PTR)ConfigDir->SecurityCookie;

    if ((PCHAR)Cookie <= (PCHAR)BaseAddress ||
        (PCHAR)Cookie >= (PCHAR)BaseAddress + SizeOfImage - sizeof(*Cookie))
    {
        Cookie = NULL;
    }

    return Cookie;
}

PVOID
NTAPI
LdrpInitSecurityCookie(PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PULONG_PTR Cookie;
    ULONG_PTR NewCookie;

    Cookie = LdrpFetchAddressOfSecurityCookie(LdrEntry->DllBase, LdrEntry->SizeOfImage);

    if (!Cookie)
        return NULL;

    if ((*Cookie == DEFAULT_SECURITY_COOKIE) ||
        (*Cookie == 0))
    {
        LARGE_INTEGER Counter = KeQueryPerformanceCounter(NULL);

        NewCookie = (ULONG_PTR)Cookie;

        NewCookie ^= (ULONG_PTR)Counter.LowPart;
#ifdef _WIN64

        if (NewCookie > COOKIE_MAX)
        {
            NewCookie >>= 16;
        }
#endif

        if ((NewCookie == 0) || (NewCookie == *Cookie))
        {
            NewCookie = DEFAULT_SECURITY_COOKIE + 1;
        }

        *Cookie = NewCookie;
    }

    return Cookie;
}

VOID
NTAPI
MmFreePreparedSystemImage(
    _Inout_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage)
{
    PAGED_CODE();

    if (PreparedImage->Section) ObDereferenceObject(PreparedImage->Section);
    if (PreparedImage->FileHandle) ZwClose(PreparedImage->FileHandle);
    RtlZeroMemory(PreparedImage, sizeof(*PreparedImage));
}

NTSTATUS
NTAPI
MmPrepareSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_ ULONG Flags,
    _Out_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE SectionHandle;
    NTSTATUS Status;

    PAGED_CODE();
    RtlZeroMemory(PreparedImage, sizeof(*PreparedImage));

    InitializeObjectAttributes(&ObjectAttributes,
                               FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenFile(&PreparedImage->FileHandle,
                       FILE_EXECUTE,
                       &ObjectAttributes,
                       &IoStatusBlock,
                       FILE_SHARE_READ | FILE_SHARE_DELETE,
                       0);
    if (!NT_SUCCESS(Status)) goto Failure;

    Status = MmCheckSystemImage(PreparedImage->FileHandle);
    if ((Status == STATUS_IMAGE_CHECKSUM_MISMATCH) ||
        (Status == STATUS_IMAGE_MP_UP_MISMATCH) ||
        (Status == STATUS_INVALID_IMAGE_PROTECT))
    {
        goto Failure;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateSection(&SectionHandle,
                             Flags ? SECTION_MAP_READ | SECTION_MAP_EXECUTE
                                   : SECTION_ALL_ACCESS,
                             &ObjectAttributes,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             PreparedImage->FileHandle);
    if (!NT_SUCCESS(Status)) goto Failure;

    Status = ObReferenceObjectByHandle(SectionHandle,
                                       SECTION_MAP_EXECUTE,
                                       MmSectionObjectType,
                                       KernelMode,
                                       &PreparedImage->Section,
                                       NULL);
    ZwClose(SectionHandle);
    if (!NT_SUCCESS(Status)) goto Failure;
    return STATUS_SUCCESS;

Failure:
    MmFreePreparedSystemImage(PreparedImage);
    return Status;
}

NTSTATUS
NTAPI
MmLoadSystemImageEx(
    _In_ PUNICODE_STRING FileName,
    _In_opt_ PUNICODE_STRING NamePrefix,
    _In_opt_ PUNICODE_STRING LoadedName,
    _In_ ULONG Flags,
    _Inout_opt_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage,
    _Out_ PVOID *ModuleObject,
    _Out_ PVOID *ImageBaseAddress)
{
    PVOID ModuleLoadBase = NULL;
    NTSTATUS Status;
    HANDLE FileHandle = NULL;
    PIMAGE_NT_HEADERS NtHeader;
    UNICODE_STRING BaseName, BaseDirectory, PrefixName;
    PLDR_DATA_TABLE_ENTRY LdrEntry = NULL;
    ULONG EntrySize, DriverSize;
    PLOAD_IMPORTS LoadedImports = MM_SYSLDR_NO_IMPORTS;
    PCHAR MissingApiName, Buffer;
    PWCHAR MissingDriverName, PrefixedBuffer = NULL;
    PSECTION Section = NULL;
    BOOLEAN LockOwned = FALSE;
    PLIST_ENTRY NextEntry;
    IMAGE_INFO ImageInfo;

    PAGED_CODE();

    if (Flags)
    {
        ASSERT(NamePrefix == NULL);
        ASSERT(LoadedName == NULL);

        if (!PsGetCurrentProcess()->ProcessInSession) return STATUS_NO_MEMORY;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   MAXIMUM_FILENAME_LENGTH,
                                   TAG_LDR_WSTR);
    if (!Buffer) return STATUS_INSUFFICIENT_RESOURCES;

    if (FileName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        PWCHAR p;
        ULONG BaseLength;

        p = &FileName->Buffer[FileName->Length / sizeof(WCHAR)];
        while (*(p - 1) != OBJ_NAME_PATH_SEPARATOR) p--;

        BaseLength = (ULONG)(&FileName->Buffer[FileName->Length / sizeof(WCHAR)] - p);
        BaseLength *= sizeof(WCHAR);

        BaseName.Length = (USHORT)BaseLength;
        BaseName.Buffer = p;
    }
    else
    {
        BaseName.Length = FileName->Length;
        BaseName.Buffer = FileName->Buffer;
    }

    BaseName.MaximumLength = BaseName.Length;

    BaseDirectory = *FileName;
    BaseDirectory.Length -= BaseName.Length;
    BaseDirectory.MaximumLength = BaseDirectory.Length;

    PrefixName = *FileName;

    if (NamePrefix)
    {
        Status = RtlUShortAdd(BaseDirectory.Length,
                              NamePrefix->Length,
                              &PrefixName.MaximumLength);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Quickie;
        }

        Status = RtlUShortAdd(PrefixName.MaximumLength,
                              BaseName.Length,
                              &PrefixName.MaximumLength);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Quickie;
        }

        PrefixedBuffer = ExAllocatePoolWithTag(PagedPool,
                                               PrefixName.MaximumLength,
                                               TAG_LDR_WSTR);
        if (!PrefixedBuffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Quickie;
        }

        PrefixName.Buffer = PrefixedBuffer;
        PrefixName.Length = 0;

        RtlAppendUnicodeStringToString(&PrefixName, &BaseDirectory);
        RtlAppendUnicodeStringToString(&PrefixName, NamePrefix);
        RtlAppendUnicodeStringToString(&PrefixName, &BaseName);

        BaseName.Buffer = &(PrefixName.Buffer[BaseDirectory.Length / sizeof(WCHAR)]);
        BaseName.Length += NamePrefix->Length;
        BaseName.MaximumLength = (PrefixName.MaximumLength - BaseDirectory.Length);
    }

    if (LoadedName) BaseName = *LoadedName;

    if (NtGlobalFlag & FLG_SHOW_LDR_SNAPS)
    {
        DPRINT1("MM:SYSLDR Loading %wZ (%wZ) %s\n",
                &PrefixName, &BaseName, Flags ? "in session space" : "");
    }

LoaderScan:
    ASSERT(LockOwned == FALSE);
    LockOwned = TRUE;
    KeEnterCriticalRegion();
    KeWaitForSingleObject(&MmSystemLoadLock,
                          WrVirtualMemory,
                          KernelMode,
                          FALSE,
                          NULL);

    NextEntry = PsLoadedModuleList.Flink;
    while (NextEntry != &PsLoadedModuleList)
    {
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);
        if (RtlEqualUnicodeString(&PrefixName, &LdrEntry->FullDllName, TRUE))
        {
            break;
        }

        NextEntry = NextEntry->Flink;
    }

    if (NextEntry != &PsLoadedModuleList)
    {
        if (Section)
        {
            ObDereferenceObject(Section);
            Section = NULL;
        }

        if (!Flags)
        {
            *ModuleObject = LdrEntry;
            *ImageBaseAddress = LdrEntry->DllBase;
            Status = STATUS_IMAGE_ALREADY_LOADED;
        }
        else
        {
            UNIMPLEMENTED_DBGBREAK("Unsupported Session-Load!\n");
            Status = STATUS_NOT_IMPLEMENTED;
        }

        goto Quickie;
    }
    else if (!Section)
    {
        KeReleaseMutant(&MmSystemLoadLock, MUTANT_INCREMENT, FALSE, FALSE);
        KeLeaveCriticalRegion();
        LockOwned = FALSE;

        if ((KdDebuggerEnabled) && !(KdDebuggerNotPresent))
        {
        }

        LdrEntry = NULL;

        if (PreparedImage && PreparedImage->Section)
        {
            FileHandle = PreparedImage->FileHandle;
            Section = PreparedImage->Section;
            RtlZeroMemory(PreparedImage, sizeof(*PreparedImage));
        }
        else
        {
            MM_PREPARED_SYSTEM_IMAGE Image;

            Status = MmPrepareSystemImage(FileName, Flags, &Image);
            if (!NT_SUCCESS(Status)) goto Quickie;
            FileHandle = Image.FileHandle;
            Section = Image.Section;
        }

        if (Flags)
        {
            UNIMPLEMENTED_DBGBREAK("Unsupported Session-Load!\n");
            goto Quickie;
        }

        goto LoaderScan;
    }
    else
    {
        LdrEntry = NULL;
    }

    Status = MiLoadImageSection(&Section,
                                &ModuleLoadBase,
                                FileName,
                                FALSE,
                                NULL);
    ASSERT(Status != STATUS_ALREADY_COMMITTED);

    DriverSize = 0;
    if (NT_SUCCESS(Status))
    {
        PIMAGE_NT_HEADERS LoadedHeaders = RtlImageNtHeader(ModuleLoadBase);

        if (LoadedHeaders != NULL)
            DriverSize = (ULONG)ROUND_TO_PAGES(LoadedHeaders->OptionalHeader.SizeOfImage);
    }

    if (!Flags)
    {
        if (NT_SUCCESS(Status))
        {
            MiUseLargeDriverPage(DriverSize / PAGE_SIZE,
                                 &ModuleLoadBase,
                                 &BaseName,
                                 TRUE);
        }

        ObDereferenceObject(Section);
        Section = NULL;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiLoadImageSection failed with status 0x%x\n", Status);
        goto Quickie;
    }

    Status = LdrRelocateImageWithBias(ModuleLoadBase,
                                      0,
                                      "SYSLDR",
                                      STATUS_SUCCESS,
                                      STATUS_CONFLICTING_ADDRESSES,
                                      STATUS_INVALID_IMAGE_FORMAT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("LdrRelocateImageWithBias failed with status 0x%x\n", Status);
        goto Quickie;
    }

    NtHeader = RtlImageNtHeader(ModuleLoadBase);

    EntrySize = sizeof(LDR_DATA_TABLE_ENTRY) +
                BaseName.Length +
                sizeof(UNICODE_NULL);

    LdrEntry = ExAllocatePoolWithTag(NonPagedPool, EntrySize, TAG_MODULE_OBJECT);
    if (!LdrEntry)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quickie;
    }

    LdrEntry->Flags = LDRP_LOAD_IN_PROGRESS;
    LdrEntry->LoadCount = 1;
    LdrEntry->LoadedImports = LoadedImports;
    LdrEntry->PatchInformation = NULL;

    if ((NtHeader->OptionalHeader.MajorOperatingSystemVersion >= 5) &&
        (NtHeader->OptionalHeader.MajorImageVersion >= 5))
    {
        LdrEntry->Flags |= LDRP_ENTRY_NATIVE;
    }

    LdrEntry->DllBase = ModuleLoadBase;
    LdrEntry->EntryPoint = (PVOID)((ULONG_PTR)ModuleLoadBase +
                                   NtHeader->OptionalHeader.AddressOfEntryPoint);
    LdrEntry->SizeOfImage = DriverSize;
    LdrEntry->CheckSum = NtHeader->OptionalHeader.CheckSum;
    LdrEntry->SectionPointer = Section;

    LdrEntry->BaseDllName.Buffer = (PVOID)(LdrEntry + 1);
    LdrEntry->BaseDllName.Length = BaseName.Length;
    LdrEntry->BaseDllName.MaximumLength = BaseName.Length;

    RtlCopyMemory(LdrEntry->BaseDllName.Buffer,
                  BaseName.Buffer,
                  BaseName.Length);
    LdrEntry->BaseDllName.Buffer[BaseName.Length / sizeof(WCHAR)] = UNICODE_NULL;

    LdrEntry->FullDllName.Buffer = ExAllocatePoolWithTag(PagedPool,
                                                         PrefixName.Length +
                                                         sizeof(UNICODE_NULL),
                                                         TAG_LDR_WSTR);
    if (!LdrEntry->FullDllName.Buffer)
    {
        LdrEntry->FullDllName.Length = 0;
        LdrEntry->FullDllName.MaximumLength = 0;
    }
    else
    {
        LdrEntry->FullDllName.Length = PrefixName.Length;
        LdrEntry->FullDllName.MaximumLength = PrefixName.Length;

        RtlCopyMemory(LdrEntry->FullDllName.Buffer,
                      PrefixName.Buffer,
                      PrefixName.Length);
        LdrEntry->FullDllName.Buffer[PrefixName.Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    MiProcessLoaderEntry(LdrEntry, TRUE);

    MissingApiName = Buffer;
    MissingDriverName = NULL;
    Status = MiResolveImageReferences(ModuleLoadBase,
                                      &BaseDirectory,
                                      NULL,
                                      &MissingApiName,
                                      &MissingDriverName,
                                      &LoadedImports);
    if (!NT_SUCCESS(Status))
    {
        BOOLEAN NeedToFreeString = FALSE;

        if (*(ULONG_PTR*)&MissingDriverName & 1)
        {
            NeedToFreeString = TRUE;
            *(ULONG_PTR*)&MissingDriverName &= ~1;
        }

        DPRINT1("MiResolveImageReferences failed with status 0x%x\n", Status);
        DPRINT1(" Missing driver '%ls', missing API '%s'\n",
                MissingDriverName, MissingApiName);

        if (NeedToFreeString)
        {
            ExFreePoolWithTag(MissingDriverName, TAG_LDR_WSTR);
        }

        MiProcessLoaderEntry(LdrEntry, FALSE);

        if (LdrEntry->FullDllName.Buffer)
        {
            ExFreePoolWithTag(LdrEntry->FullDllName.Buffer, TAG_LDR_WSTR);
        }

        ExFreePoolWithTag(LdrEntry, TAG_MODULE_OBJECT);
        LdrEntry = NULL;
        goto Quickie;
    }

    LdrEntry->Flags |= (LDRP_SYSTEM_MAPPED |
                        LDRP_ENTRY_PROCESSED |
                        LDRP_MM_LOADED);
    LdrEntry->Flags &= ~LDRP_LOAD_IN_PROGRESS;
    LdrEntry->LoadedImports = LoadedImports;

    MiWriteProtectSystemImage(LdrEntry->DllBase);

    LdrpInitSecurityCookie(LdrEntry);

    if (PsImageNotifyEnabled)
    {
        ImageInfo.Properties = 0;
        ImageInfo.ImageAddressingMode = IMAGE_ADDRESSING_MODE_32BIT;
        ImageInfo.SystemModeImage = TRUE;
        ImageInfo.ImageSize = LdrEntry->SizeOfImage;
        ImageInfo.ImageBase = LdrEntry->DllBase;
        ImageInfo.ImageSectionNumber = ImageInfo.ImageSelector = 0;

        PspRunLoadImageNotifyRoutines(FileName, NULL, &ImageInfo);
    }

#ifdef __ROS_ROSSYM__

    if (TRUE)
#else

    if (MiCacheImageSymbols(LdrEntry->DllBase))
#endif
    {
        UNICODE_STRING UnicodeTemp;
        STRING AnsiTemp;

        if ((PrefixName.Length > (11 * sizeof(WCHAR))) &&
            !(_wcsnicmp(PrefixName.Buffer, L"\\SystemRoot", 11)))
        {
            UnicodeTemp = PrefixName;
            UnicodeTemp.Buffer += 11;
            UnicodeTemp.Length -= (11 * sizeof(WCHAR));
            RtlStringCbPrintfA(Buffer,
                               MAXIMUM_FILENAME_LENGTH,
                               "%ws%wZ",
                               &SharedUserData->NtSystemRoot[2],
                               &UnicodeTemp);
        }
        else
        {
            RtlStringCbPrintfA(Buffer, MAXIMUM_FILENAME_LENGTH,
                               "%wZ", &BaseName);
        }

        RtlInitString(&AnsiTemp, Buffer);

        DbgLoadImageSymbols(&AnsiTemp,
                            LdrEntry->DllBase,
                            (ULONG_PTR)PsGetCurrentProcessId());
        LdrEntry->Flags |= LDRP_DEBUG_SYMBOLS_LOADED;
    }

    ASSERT(Section == NULL);
    MiEnablePagingOfDriver(LdrEntry);

    *ModuleObject = LdrEntry;
    *ImageBaseAddress = LdrEntry->DllBase;

Quickie:

    if (LockOwned)
    {
        KeReleaseMutant(&MmSystemLoadLock, MUTANT_INCREMENT, FALSE, FALSE);
        KeLeaveCriticalRegion();
        LockOwned = FALSE;
    }

    if (Section) ObDereferenceObject(Section);

    if (FileHandle) ZwClose(FileHandle);

    if (PrefixedBuffer) ExFreePoolWithTag(PrefixedBuffer, TAG_LDR_WSTR);

    ExFreePoolWithTag(Buffer, TAG_LDR_WSTR);
    return Status;
}

NTSTATUS
NTAPI
MmLoadSystemImage(IN PUNICODE_STRING FileName,
                  IN PUNICODE_STRING NamePrefix OPTIONAL,
                  IN PUNICODE_STRING LoadedName OPTIONAL,
                  IN ULONG Flags,
                  OUT PVOID *ModuleObject,
                  OUT PVOID *ImageBaseAddress)
{
    return MmLoadSystemImageEx(FileName,
                               NamePrefix,
                               LoadedName,
                               Flags,
                               NULL,
                               ModuleObject,
                               ImageBaseAddress);
}

PLDR_DATA_TABLE_ENTRY
NTAPI
MiLookupDataTableEntry(IN PVOID Address)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry, FoundEntry = NULL;
    PLIST_ENTRY NextEntry;
    PAGED_CODE();

    NextEntry = PsLoadedModuleList.Flink;
    do
    {
        LdrEntry =  CONTAINING_RECORD(NextEntry,
                                      LDR_DATA_TABLE_ENTRY,
                                      InLoadOrderLinks);

        if ((Address >= LdrEntry->DllBase) &&
            (Address < (PVOID)((ULONG_PTR)LdrEntry->DllBase +
                               LdrEntry->SizeOfImage)))
        {
            FoundEntry = LdrEntry;
            break;
        }

        NextEntry = NextEntry->Flink;
    } while(NextEntry != &PsLoadedModuleList);

    return FoundEntry;
}

BOOLEAN
NTAPI
MmIsSystemImageImportingModule(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ModuleName)
{
    PLOAD_IMPORTS LoadedImports;
    PLDR_DATA_TABLE_ENTRY ImportEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    BOOLEAN Found = FALSE;
    SIZE_T Index;

    PAGED_CODE();

    if (!ImageBase || !ModuleName || !ModuleName->Buffer)
        return FALSE;

    if (!PsLoadedModuleList.Flink || !PsLoadedModuleList.Blink)
        return FALSE;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    LdrEntry = MiLookupDataTableEntry(ImageBase);
    if (!LdrEntry)
        goto Exit;

    LoadedImports = LdrEntry->LoadedImports;
    if (!LoadedImports || (LoadedImports == MM_SYSLDR_NO_IMPORTS) || (LoadedImports == MM_SYSLDR_BOOT_LOADED))
        goto Exit;

    if ((ULONG_PTR)LoadedImports & MM_SYSLDR_SINGLE_ENTRY)
    {
        ImportEntry = (PVOID)((ULONG_PTR)LoadedImports & ~MM_SYSLDR_SINGLE_ENTRY);
        Found = RtlEqualUnicodeString(&ImportEntry->BaseDllName, ModuleName, TRUE);
        goto Exit;
    }

    for (Index = 0; Index < LoadedImports->Count; Index++)
    {
        ImportEntry = LoadedImports->Entry[Index];
        if (ImportEntry && RtlEqualUnicodeString(&ImportEntry->BaseDllName, ModuleName, TRUE))
        {
            Found = TRUE;
            break;
        }
    }

Exit:
    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();
    return Found;
}

PVOID
NTAPI
MmPageEntireDriver(IN PVOID AddressWithinSection)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PAGED_CODE();

    LdrEntry = MiLookupDataTableEntry(AddressWithinSection);
    return LdrEntry ? LdrEntry->DllBase : NULL;
}

VOID
NTAPI
MmResetDriverPaging(IN PVOID AddressWithinSection)
{
    UNREFERENCED_PARAMETER(AddressWithinSection);
    ASSERT(MmDisablePagingExecutive);
}

PVOID
NTAPI
MmGetSystemRoutineAddress(IN PUNICODE_STRING SystemRoutineName)
{
    PVOID ProcAddress = NULL;
    ANSI_STRING AnsiRoutineName;
    NTSTATUS Status;
    PLIST_ENTRY NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    BOOLEAN Found = FALSE;
    UNICODE_STRING KernelName = RTL_CONSTANT_STRING(L"ntoskrnl.exe");
    UNICODE_STRING HalName = RTL_CONSTANT_STRING(L"hal.dll");
    ULONG Modules = 0;

    Status = RtlUnicodeStringToAnsiString(&AnsiRoutineName,
                                          SystemRoutineName,
                                          TRUE);
    if (!NT_SUCCESS(Status)) return NULL;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    NextEntry = PsLoadedModuleList.Flink;
    while (NextEntry != &PsLoadedModuleList)
    {
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if (RtlEqualUnicodeString(&KernelName, &LdrEntry->BaseDllName, TRUE))
        {
            Found = TRUE;
            Modules++;
        }
        else if (RtlEqualUnicodeString(&HalName, &LdrEntry->BaseDllName, TRUE))
        {
            Found = TRUE;
            Modules++;
        }

        if (Found)
        {
            ProcAddress = RtlFindExportedRoutineByName(LdrEntry->DllBase,
                                                       AnsiRoutineName.Buffer);

            if (ProcAddress) break;
            if (Modules == 2) break;
        }

        NextEntry = NextEntry->Flink;
    }

    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();

    RtlFreeAnsiString(&AnsiRoutineName);
    return ProcAddress;
}

