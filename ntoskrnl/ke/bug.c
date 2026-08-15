/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/bug.c
 * PURPOSE:         Bugcheck Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <internal/dump.h>

#ifdef KDBG
#include <kdbg/kdb.h>
#endif

#define NDEBUG
#include <debug.h>
/* miarm.h uses DPRINT, so it must follow debug.h. */
#include <mm/ARM3/miarm.h>

#define KI_BUGCHECK_BACKTRACE_FRAMES 8
#define KI_BUGCHECK_MAX_MODULES 4096

/* GLOBALS *******************************************************************/

LIST_ENTRY KeBugcheckCallbackListHead;
LIST_ENTRY KeBugcheckReasonCallbackListHead;
KSPIN_LOCK BugCheckCallbackLock;
ULONG KeBugCheckActive, KeBugCheckOwner;
LONG KeBugCheckOwnerRecursionCount;
PMESSAGE_RESOURCE_DATA KiBugCodeMessages;
ULONG KeBugCheckCount = 1;
ULONG KiHardwareTrigger;
PUNICODE_STRING KiBugCheckDriver;
ULONG_PTR KiBugCheckData[5];

PKNMI_HANDLER_CALLBACK KiNmiCallbackListHead = NULL;
KSPIN_LOCK KiNmiCallbackListLock;

/* Jira Reporting */
UNICODE_STRING KeRosProcessorName, KeRosBiosDate, KeRosBiosVersion;
UNICODE_STRING KeRosVideoBiosDate, KeRosVideoBiosVersion;

/* PRIVATE FUNCTIONS *********************************************************/

static
BOOLEAN
KiIsScreenDebuggingEnabled(VOID)
{
    static const CHAR ScreenOption[] = "DEBUGPORT=SCREEN";
    PCSTR Current;
    SIZE_T OptionLength;

    if (!KeLoaderBlock || !KeLoaderBlock->LoadOptions)
        return FALSE;

    Current = KeLoaderBlock->LoadOptions;
    while (*Current)
    {
        Current += strspn(Current, " \t/");
        OptionLength = strcspn(Current, " \t/");

        if ((OptionLength == sizeof(ScreenOption) - sizeof(ANSI_NULL)) &&
            (_strnicmp(Current, ScreenOption, OptionLength) == 0))
        {
            return TRUE;
        }

        Current += OptionLength;
    }

    return FALSE;
}

static
VOID
KiLogBugCheckString(
    _In_z_ PCSTR String)
{
    ANSI_STRING LogString;

    RtlInitAnsiString(&LogString, String);
    KdLogDbgPrint(&LogString);
}

static
VOID
KiDisplayAndLogBugCheckString(
    _In_z_ PCSTR String)
{
    InbvDisplayString((PCHAR)String);
    KiLogBugCheckString(String);
}

PVOID
NTAPI
KiPcToFileHeader(IN PVOID Pc,
                 OUT PLDR_DATA_TABLE_ENTRY *LdrEntry,
                 IN BOOLEAN DriversOnly,
                 OUT PBOOLEAN InKernel)
{
    ULONG i;
    PVOID ImageBase, PcBase = NULL;
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY ListHead, NextEntry;
    PLIST_ENTRY ListHeads[2];
    ULONG ListCount = 0, ListIndex;

    if ((PsLoadedModuleList.Flink != NULL) &&
        (PsLoadedModuleList.Blink != NULL) &&
        (PsLoadedModuleList.Flink != &PsLoadedModuleList))
    {
        ListHeads[ListCount++] = &PsLoadedModuleList;
    }

    if (KeLoaderBlock)
    {
        ListHeads[ListCount++] = &KeLoaderBlock->LoadOrderListHead;
    }

    /* Assume no */
    *InKernel = FALSE;

    for (ListIndex = 0; ListIndex < ListCount; ListIndex++)
    {
        ListHead = ListHeads[ListIndex];

        /* Set list pointers and make sure it's valid */
        NextEntry = ListHead->Flink;
        if (!NextEntry)
        {
            continue;
        }

        /* Start loop */
        i = 0;
        while (NextEntry != ListHead)
        {
            /* Keep a torn loader list from hanging the bugcheck path. */
            if (++i > KI_BUGCHECK_MAX_MODULES)
                break;

            /* Check if this is a kernel entry and we only want drivers */
            if ((i <= 2) && (DriversOnly != FALSE))
            {
                /* Skip it */
                NextEntry = NextEntry->Flink;
                continue;
            }

            /* Get the loader entry */
            Entry = CONTAINING_RECORD(NextEntry,
                                      LDR_DATA_TABLE_ENTRY,
                                      InLoadOrderLinks);

            /* Move to the next entry */
            NextEntry = NextEntry->Flink;
            ImageBase = Entry->DllBase;

            /* Check if this is the right one */
            if (((ULONG_PTR)Pc >= (ULONG_PTR)Entry->DllBase) &&
                ((ULONG_PTR)Pc < ((ULONG_PTR)Entry->DllBase + Entry->SizeOfImage)))
            {
                /* Return this entry */
                *LdrEntry = Entry;
                PcBase = ImageBase;

                /* Check if this was a kernel or HAL entry */
                if (i <= 2) *InKernel = TRUE;
                break;
            }
        }

        if (PcBase)
        {
            break;
        }
    }

    /* Return the base address */
    return PcBase;
}

PVOID
NTAPI
KiRosPcToUserFileHeader(IN PVOID Pc,
                        OUT PLDR_DATA_TABLE_ENTRY *LdrEntry)
{
    PVOID ImageBase, PcBase = NULL;
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY ListHead, NextEntry;

    /*
     * We know this is valid because we should only be called after a
     * succesfull address from RtlWalkFrameChain for UserMode, which
     * validates everything for us.
     */
    ListHead = &KeGetCurrentThread()->
               Teb->ProcessEnvironmentBlock->Ldr->InLoadOrderModuleList;

    /* Set list pointers and make sure it's valid */
    NextEntry = ListHead->Flink;
    if (NextEntry)
    {
        /* Start loop */
        while (NextEntry != ListHead)
        {
            /* Get the loader entry */
            Entry = CONTAINING_RECORD(NextEntry,
                                      LDR_DATA_TABLE_ENTRY,
                                      InLoadOrderLinks);

            /* Move to the next entry */
            NextEntry = NextEntry->Flink;
            ImageBase = Entry->DllBase;

            /* Check if this is the right one */
            if (((ULONG_PTR)Pc >= (ULONG_PTR)Entry->DllBase) &&
                ((ULONG_PTR)Pc < ((ULONG_PTR)Entry->DllBase + Entry->SizeOfImage)))
            {
                /* Return this entry */
                *LdrEntry = Entry;
                PcBase = ImageBase;
                break;
            }
        }
    }

    /* Return the base address */
    return PcBase;
}

USHORT
NTAPI
KeRosCaptureUserStackBackTrace(IN ULONG FramesToSkip,
                               IN ULONG FramesToCapture,
                               OUT PVOID *BackTrace,
                               OUT PULONG BackTraceHash OPTIONAL)
{
    PVOID Frames[2 * 64];
    ULONG FrameCount;
    ULONG Hash = 0, i;

    /* Skip a frame for the caller */
    FramesToSkip++;

    /* Don't go past the limit */
    if ((FramesToCapture + FramesToSkip) >= 128) return 0;

    /* Do the back trace */
    FrameCount = RtlWalkFrameChain(Frames, FramesToCapture + FramesToSkip, 1);

    /* Make sure we're not skipping all of them */
    if (FrameCount <= FramesToSkip) return 0;

    /* Loop all the frames */
    for (i = 0; i < FramesToCapture; i++)
    {
        /* Don't go past the limit */
        if ((FramesToSkip + i) >= FrameCount) break;

        /* Save this entry and hash it */
        BackTrace[i] = Frames[FramesToSkip + i];
        Hash += PtrToUlong(BackTrace[i]);
    }

    /* Write the hash */
    if (BackTraceHash) *BackTraceHash = Hash;

    /* Clear the other entries and return count */
    RtlFillMemoryUlong(Frames, 128, 0);
    return (USHORT)i;
}


VOID
FASTCALL
KeRosDumpStackFrameArray(IN PULONG_PTR Frames,
                         IN ULONG FrameCount)
{
    ULONG i;
    ULONG_PTR Addr;
    BOOLEAN InSystem;
    PVOID p;

    /* GCC complaints that it may be used uninitialized */
    PLDR_DATA_TABLE_ENTRY LdrEntry = NULL;

    /* Loop them */
    for (i = 0; i < FrameCount; i++)
    {
        /* Get the EIP */
        Addr = Frames[i];
        if (!Addr)
        {
        	break;
        }

        /* Get the base for this file */
        if (Addr > (ULONG_PTR)MmHighestUserAddress)
        {
            /* We are in kernel */
            p = KiPcToFileHeader((PVOID)Addr, &LdrEntry, FALSE, &InSystem);
        }
        else
        {
            /* We are in user land */
            p = KiRosPcToUserFileHeader((PVOID)Addr, &LdrEntry);
        }
        if (p)
        {
#ifdef KDBG
            if (!KdbSymPrintAddress((PVOID)Addr, NULL))
#endif
            {
                CHAR AnsiName[64];

                /* Convert module name to ANSI and print it */
                KeBugCheckUnicodeToAnsi(&LdrEntry->BaseDllName,
                                        AnsiName,
                                        sizeof(AnsiName));
                Addr -= (ULONG_PTR)LdrEntry->DllBase;
                DbgPrint("<%s: %p>", AnsiName, (PVOID)Addr);
            }
        }
        else
        {
            /* Print only the address */
            DbgPrint("<%p>", (PVOID)Addr);
        }

        /* Go to the next frame */
        DbgPrint("\n");
    }
}

VOID
NTAPI
KeRosDumpStackFrames(IN PULONG_PTR Frame OPTIONAL,
                     IN ULONG FrameCount OPTIONAL)
{
    ULONG_PTR Frames[32];
    ULONG RealFrameCount;

    /* If the caller didn't ask, assume 32 frames */
    if (!FrameCount || FrameCount > 32) FrameCount = 32;

    if (Frame)
    {
        /* Dump them */
        KeRosDumpStackFrameArray(Frame, FrameCount);
    }
    else
    {
        /* Get the current frames (skip the two. One for the dumper, one for the caller) */
        RealFrameCount = RtlCaptureStackBackTrace(2, FrameCount, (PVOID*)Frames, NULL);
        DPRINT1("RealFrameCount =%lu\n", RealFrameCount);

        /* Dump them */
        KeRosDumpStackFrameArray(Frames, RealFrameCount);

        /* Count left for user mode? */
        if (FrameCount - RealFrameCount > 0)
        {
            /* Get the current frames */
            RealFrameCount = KeRosCaptureUserStackBackTrace(-1, FrameCount - RealFrameCount, (PVOID*)Frames, NULL);

            /* Dump them */
            KeRosDumpStackFrameArray(Frames, RealFrameCount);
        }
    }
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeBugCheck(VOID)
{
    PMESSAGE_RESOURCE_DATA BugCheckData;
    LDR_RESOURCE_INFO ResourceInfo;
    PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry;
    NTSTATUS Status;
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    /* Get the kernel entry */
    LdrEntry = CONTAINING_RECORD(KeLoaderBlock->LoadOrderListHead.Flink,
                                 LDR_DATA_TABLE_ENTRY,
                                 InLoadOrderLinks);

    /* Cache the bugcheck message strings. Prepare the lookup data. */
    ResourceInfo.Type = RT_MESSAGETABLE;
    ResourceInfo.Name = 1;
    ResourceInfo.Language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    /* Do the lookup */
    Status = LdrFindResource_U(LdrEntry->DllBase,
                               &ResourceInfo,
                               RESOURCE_DATA_LEVEL,
                               &ResourceDataEntry);

    /* Make sure it worked */
    if (NT_SUCCESS(Status))
    {
        /* Now actually get a pointer to it */
        Status = LdrAccessResource(LdrEntry->DllBase,
                                   ResourceDataEntry,
                                   (PVOID*)&BugCheckData,
                                   NULL);
        if (NT_SUCCESS(Status)) KiBugCodeMessages = BugCheckData;
    }
}

BOOLEAN
NTAPI
KeGetBugMessageText(IN ULONG BugCheckCode,
                    OUT PANSI_STRING OutputString OPTIONAL)
{
    ULONG i;
    ULONG IdOffset;
    PMESSAGE_RESOURCE_ENTRY MessageEntry;
    PCHAR BugCode;
    USHORT Length;
    BOOLEAN Result = FALSE;

    /* Make sure we're not bugchecking too early */
    if (!KiBugCodeMessages) return Result;

    /*
     * Globally protect in SEH as we are trying to access data in
     * dire situations, and potentially going to patch it (see below).
     */
    _SEH2_TRY
    {

    /*
     * Make the kernel resource section writable, as we are going to manually
     * trim the trailing newlines in the bugcheck resource message in place,
     * when OutputString is NULL and before displaying it on screen.
     */
    MmMakeKernelResourceSectionWritable();

    /* Find the message. This code is based on RtlFindMesssage */
    for (i = 0; i < KiBugCodeMessages->NumberOfBlocks; i++)
    {
        /* Check if the ID matches */
        if ((BugCheckCode >= KiBugCodeMessages->Blocks[i].LowId) &&
            (BugCheckCode <= KiBugCodeMessages->Blocks[i].HighId))
        {
            /* Get offset to entry */
            MessageEntry = (PMESSAGE_RESOURCE_ENTRY)
                ((ULONG_PTR)KiBugCodeMessages + KiBugCodeMessages->Blocks[i].OffsetToEntries);
            IdOffset = BugCheckCode - KiBugCodeMessages->Blocks[i].LowId;

            /* Advance in the entries until finding it */
            while (IdOffset--)
            {
                MessageEntry = (PMESSAGE_RESOURCE_ENTRY)
                    ((ULONG_PTR)MessageEntry + MessageEntry->Length);
            }

            /* Make sure it's not Unicode */
            ASSERT(!(MessageEntry->Flags & MESSAGE_RESOURCE_UNICODE));

            /* Get the final code */
            BugCode = (PCHAR)MessageEntry->Text;
            Length = (USHORT)strlen(BugCode);

            /* Handle trailing newlines */
            while ((Length > 0) && ((BugCode[Length - 1] == '\n') ||
                                    (BugCode[Length - 1] == '\r') ||
                                    (BugCode[Length - 1] == ANSI_NULL)))
            {
                /* Directly trim the newline in place if we don't return the string */
                if (!OutputString) BugCode[Length - 1] = ANSI_NULL;

                /* Skip the trailing newline */
                Length--;
            }

            /* Check if caller wants an output string */
            if (OutputString)
            {
                /* Return it in the OutputString */
                OutputString->Buffer = BugCode;
                OutputString->Length = Length;
                OutputString->MaximumLength = Length;
            }
            else
            {
                /* Direct output to screen */
                InbvDisplayString(BugCode);
                InbvDisplayString("\r");
            }

            /* We're done */
            Result = TRUE;
            break;
        }
    }

    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    /* Return the result */
    return Result;
}

VOID
NTAPI
KiDoBugCheckCallbacks(VOID)
{
    PKBUGCHECK_CALLBACK_RECORD CurrentRecord;
    PLIST_ENTRY ListHead, NextEntry, LastEntry;
    ULONG_PTR Checksum;

    /* First make sure that the list is initialized... it might not be */
    ListHead = &KeBugcheckCallbackListHead;
    if ((!ListHead->Flink) || (!ListHead->Blink))
        return;

    /* Loop the list */
    LastEntry = ListHead;
    NextEntry = ListHead->Flink;
    while (NextEntry != ListHead)
    {
        /* Get the record */
        CurrentRecord = CONTAINING_RECORD(NextEntry,
                                          KBUGCHECK_CALLBACK_RECORD,
                                          Entry);

        /* Validate it */
        // TODO/FIXME: Check whether the memory CurrentRecord points to
        // is still accessible and valid!
        if (CurrentRecord->Entry.Blink != LastEntry) return;
        Checksum = (ULONG_PTR)CurrentRecord->CallbackRoutine;
        Checksum += (ULONG_PTR)CurrentRecord->Buffer;
        Checksum += (ULONG_PTR)CurrentRecord->Length;
        Checksum += (ULONG_PTR)CurrentRecord->Component;

        /* Make sure it's inserted and validated */
        if ((CurrentRecord->State == BufferInserted) &&
            (CurrentRecord->Checksum == Checksum))
        {
            /* Call the routine */
            CurrentRecord->State = BufferStarted;
            _SEH2_TRY
            {
                (CurrentRecord->CallbackRoutine)(CurrentRecord->Buffer,
                                                 CurrentRecord->Length);
                CurrentRecord->State = BufferFinished;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                CurrentRecord->State = BufferIncomplete;
            }
            _SEH2_END;
        }

        /* Go to the next entry */
        LastEntry = NextEntry;
        NextEntry = NextEntry->Flink;
    }
}

VOID
NTAPI
KiBugCheckDebugBreak(IN ULONG StatusCode)
{
    /*
     * Wrap this in SEH so we don't crash if
     * there is no debugger or if it disconnected
     */
DoBreak:
    _SEH2_TRY
    {
        /* Breakpoint */
        DbgBreakPointWithStatus(StatusCode);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* No debugger, halt the CPU */
        HalHaltSystem();
    }
    _SEH2_END;

    /* Break again if this wasn't first try */
    if (StatusCode != DBG_STATUS_BUGCHECK_FIRST) goto DoBreak;
}

PCHAR
NTAPI
KeBugCheckUnicodeToAnsi(IN PUNICODE_STRING Unicode,
                        OUT PCHAR Ansi,
                        IN ULONG Length)
{
    PCHAR p;
    PWCHAR pw;
    ULONG i;

    /* Set length and normalize it */
    i = Unicode->Length / sizeof(WCHAR);
    i = min(i, Length - 1);

    /* Set source and destination, and copy */
    pw = Unicode->Buffer;
    p = Ansi;
    while (i--) *p++ = (CHAR)*pw++;

    /* Null terminate and return */
    *p = ANSI_NULL;
    return Ansi;
}

#if defined(_M_AMD64) || defined(_M_ARM64)
static
BOOLEAN
KiIsBugCheckCodeAddress(
    _In_ ULONG_PTR Address)
{
    BOOLEAN InKernel;
    BOOLEAN Valid = FALSE;
    PVOID ImageBase;
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    if ((Address <= (ULONG_PTR)MmHighestUserAddress) || !MmIsAddressValid((PVOID)Address))
        return FALSE;

    _SEH2_TRY
    {
        ImageBase = KiPcToFileHeader((PVOID)Address, &LdrEntry, FALSE, &InKernel);
        Valid = (ImageBase != NULL) && (LdrEntry != NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Valid = FALSE;
    }
    _SEH2_END;

    return Valid;
}

/*
 * RtlLookupFunctionEntry takes the loaded-module spin lock. The other CPUs
 * are frozen during a bugcheck, so use the bounded lock-free module walk and
 * inspect the static image exception directory directly instead.
 */
static
PRUNTIME_FUNCTION
KiLookupBugCheckFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG64 ImageBase)
{
    BOOLEAN InKernel;
    PVOID BaseAddress;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    IMAGE_DATA_DIRECTORY ExceptionDirectory;
    PRUNTIME_FUNCTION FunctionTable;
    PRUNTIME_FUNCTION FunctionEntry;
    PRUNTIME_FUNCTION Result = NULL;
    ULONG SizeOfImage;
    ULONG TableLength;
    ULONG IndexLow;
    ULONG IndexHigh;
    ULONG IndexMiddle;
    ULONG_PTR ControlRva;
#ifdef _M_ARM64
    ULONG FunctionLength;
    ULONG UnwindData;
    PULONG Xdata;
#endif

    *ImageBase = 0;
    if (ControlPc <= (ULONG_PTR)MmHighestUserAddress)
        return NULL;

    _SEH2_TRY
    {
        do
        {
            BaseAddress = KiPcToFileHeader((PVOID)ControlPc, &LdrEntry, FALSE, &InKernel);
            if ((BaseAddress == NULL) || (LdrEntry == NULL))
                break;

            SizeOfImage = LdrEntry->SizeOfImage;
            if (SizeOfImage < sizeof(IMAGE_NT_HEADERS))
                break;

            DosHeader = BaseAddress;
            if (!MmIsAddressValid(DosHeader) || !MmIsAddressValid((PUCHAR)DosHeader + sizeof(*DosHeader) - 1) || (DosHeader->e_magic != IMAGE_DOS_SIGNATURE) || (DosHeader->e_lfanew < 0) || ((ULONG)DosHeader->e_lfanew > SizeOfImage - sizeof(IMAGE_NT_HEADERS)))
                break;

            NtHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)BaseAddress + DosHeader->e_lfanew);
            if (!MmIsAddressValid(NtHeaders) || !MmIsAddressValid((PUCHAR)NtHeaders + sizeof(*NtHeaders) - 1) || (NtHeaders->Signature != IMAGE_NT_SIGNATURE) || (NtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) || (NtHeaders->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION))
                break;

            ExceptionDirectory = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
            if ((ExceptionDirectory.VirtualAddress == 0) || (ExceptionDirectory.Size < sizeof(RUNTIME_FUNCTION)) || ((ExceptionDirectory.Size % sizeof(RUNTIME_FUNCTION)) != 0) || (ExceptionDirectory.VirtualAddress >= SizeOfImage) || (ExceptionDirectory.Size > SizeOfImage - ExceptionDirectory.VirtualAddress))
                break;

            FunctionTable = (PRUNTIME_FUNCTION)((PUCHAR)BaseAddress + ExceptionDirectory.VirtualAddress);
            if (!MmIsAddressValid(FunctionTable) || !MmIsAddressValid((PUCHAR)FunctionTable + ExceptionDirectory.Size - 1))
                break;

            ControlRva = ControlPc - (ULONG_PTR)BaseAddress;
            TableLength = ExceptionDirectory.Size / sizeof(RUNTIME_FUNCTION);
            IndexLow = 0;
            IndexHigh = TableLength;
            while (IndexHigh > IndexLow)
            {
                IndexMiddle = IndexLow + ((IndexHigh - IndexLow) / 2);
                FunctionEntry = &FunctionTable[IndexMiddle];
                if (ControlRva < FunctionEntry->BeginAddress)
                {
                    IndexHigh = IndexMiddle;
                    continue;
                }

#ifdef _M_AMD64
                if ((FunctionEntry->BeginAddress >= FunctionEntry->EndAddress) || (FunctionEntry->EndAddress > SizeOfImage))
                    break;
                if (ControlRva >= FunctionEntry->EndAddress)
                {
                    IndexLow = IndexMiddle + 1;
                    continue;
                }
                if ((FunctionEntry->UnwindData == 0) || ((FunctionEntry->UnwindData & 0x3) != 0) || (FunctionEntry->UnwindData >= SizeOfImage) || !MmIsAddressValid((PUCHAR)BaseAddress + FunctionEntry->UnwindData))
                    break;
#else
                UnwindData = FunctionEntry->UnwindData;
                if ((UnwindData == 0) || ((UnwindData & 0x3) == 0x3))
                    break;
                if ((UnwindData & 0x3) != 0)
                {
                    FunctionLength = ((UnwindData >> 2) & 0x7ff) * sizeof(ULONG);
                }
                else
                {
                    if ((UnwindData >= SizeOfImage) || ((SizeOfImage - UnwindData) < sizeof(ULONG)))
                        break;
                    Xdata = (PULONG)((PUCHAR)BaseAddress + UnwindData);
                    if (!MmIsAddressValid(Xdata))
                        break;
                    FunctionLength = (*Xdata & 0x3ffff) * sizeof(ULONG);
                }

                if ((FunctionLength == 0) || (FunctionEntry->BeginAddress >= SizeOfImage) || (FunctionLength > SizeOfImage - FunctionEntry->BeginAddress) || (ControlRva >= FunctionEntry->BeginAddress + FunctionLength))
                {
                    IndexLow = IndexMiddle + 1;
                    continue;
                }
#endif

                *ImageBase = (ULONG64)(ULONG_PTR)BaseAddress;
                Result = FunctionEntry;
                break;
            }
        } while (FALSE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = NULL;
    }
    _SEH2_END;

    return Result;
}

static
BOOLEAN
KiUnwindBugCheckFrame(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR StackLow,
    _In_ ULONG_PTR StackHigh,
    _In_ BOOLEAN FirstFrame)
{
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase;
    ULONG64 EstablisherFrame;
    PVOID HandlerData;
    ULONG_PTR OldPc;
    ULONG_PTR OldSp;
    ULONG_PTR LookupPc;
    ULONG_PTR NewPc;
    ULONG_PTR NewSp;
    BOOLEAN Unwound = FALSE;

    OldPc = KeGetContextPc(Context);
    OldSp = KeGetContextStackRegister(Context);
    if ((OldPc == 0) || (OldSp < StackLow) || (OldSp >= StackHigh) || ((OldSp & (sizeof(ULONG_PTR) - 1)) != 0) || !KiIsBugCheckCodeAddress(OldPc))
        return FALSE;

    LookupPc = OldPc;
    if (!FirstFrame)
    {
#ifdef _M_ARM64
        if (LookupPc < sizeof(ULONG))
            return FALSE;
        LookupPc -= sizeof(ULONG);
#else
        LookupPc--;
#endif
    }

    _SEH2_TRY
    {
        FunctionEntry = KiLookupBugCheckFunctionEntry(LookupPc, &ImageBase);
        if (FunctionEntry != NULL)
        {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, LookupPc, FunctionEntry, Context, &HandlerData, &EstablisherFrame, NULL);
            Unwound = TRUE;
        }
#ifdef _M_AMD64
        else if (((StackHigh - OldSp) >= sizeof(ULONG64)) && MmIsAddressValid((PVOID)OldSp) && MmIsAddressValid((PVOID)(OldSp + sizeof(ULONG64) - 1)))
        {
            Context->Rip = *(volatile ULONG64 *)OldSp;
            Context->Rsp = OldSp + sizeof(ULONG64);
            Unwound = TRUE;
        }
#else
        else if ((Context->Lr != 0) && (Context->Lr != Context->Pc))
        {
            Context->Pc = Context->Lr;
            Unwound = TRUE;
        }
#endif
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Unwound = FALSE;
    }
    _SEH2_END;

    if (!Unwound)
        return FALSE;

    NewPc = KeGetContextPc(Context);
    NewSp = KeGetContextStackRegister(Context);
    if (!KiIsBugCheckCodeAddress(NewPc) || ((NewPc == OldPc) && (NewSp == OldSp)) || (NewSp < OldSp) || (NewSp < StackLow) || (NewSp > StackHigh))
        return FALSE;

    return TRUE;
}
#endif

static
ULONG
KiCaptureBugCheckBackTrace(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_opt_ PKTHREAD Thread,
    _Out_writes_(MaximumFrames) PULONG_PTR Frames,
    _In_ ULONG MaximumFrames)
{
    ULONG_PTR StackLow;
    ULONG_PTR StackHigh;
    ULONG_PTR Frame;
    ULONG_PTR NextFrame;
    ULONG_PTR ReturnAddress;
    ULONG_PTR ProgramCounter;
    ULONG FrameCount = 0;
#if defined(_M_AMD64) || defined(_M_ARM64)
    CONTEXT UnwindContext;
    BOOLEAN FirstFrame;
#endif

    if (MaximumFrames == 0)
        return 0;

#if defined(_M_AMD64) || defined(_M_ARM64)
    UnwindContext = *Context;
    if (TrapFrame != NULL)
    {
#ifdef _M_AMD64
        UnwindContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_AMD64;
#else
        UnwindContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_ARM64;
#endif
        KeTrapFrameToContext(TrapFrame, NULL, &UnwindContext);
    }
    ProgramCounter = KeGetContextPc(&UnwindContext);
    Frame = KeGetContextFrameRegister(&UnwindContext);
#else
    ProgramCounter = TrapFrame ? KeGetTrapFramePc(TrapFrame) : KeGetContextPc(Context);
#if defined(_M_IX86)
    Frame = TrapFrame ? KeGetTrapFrameFrameRegister(TrapFrame) : KeGetContextFrameRegister(Context);
#else
    Frame = 0;
#endif
#endif
    if (ProgramCounter == 0)
        return 0;
#if defined(_M_AMD64) || defined(_M_ARM64)
    if (!KiIsBugCheckCodeAddress(ProgramCounter))
        return 0;
#endif
    Frames[FrameCount++] = ProgramCounter;
    if (FrameCount == MaximumFrames)
        return FrameCount;
#if !defined(_M_AMD64) && !defined(_M_ARM64)
    if (Frame == 0)
        return FrameCount;
#endif

    if ((Thread == NULL) || !MmIsAddressValid(Thread) || !MmIsAddressValid(&Thread->StackLimit) || !MmIsAddressValid(&Thread->StackBase))
        return FrameCount;

    _SEH2_TRY
    {
        StackLow = (ULONG_PTR)Thread->StackLimit;
        StackHigh = (ULONG_PTR)Thread->StackBase;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        StackLow = 0;
        StackHigh = 0;
    }
    _SEH2_END;

    if ((StackLow == 0) || (StackHigh <= StackLow))
        return FrameCount;

#if defined(_M_AMD64) || defined(_M_ARM64)
    FirstFrame = TRUE;
    while (FrameCount < MaximumFrames)
    {
        if (!KiUnwindBugCheckFrame(&UnwindContext, StackLow, StackHigh, FirstFrame))
            break;

        FirstFrame = FALSE;
        ReturnAddress = KeGetContextPc(&UnwindContext);
        if (Frames[FrameCount - 1] != ReturnAddress)
            Frames[FrameCount++] = ReturnAddress;
    }

    if (FrameCount > 1)
        return FrameCount;
#endif

    /* Metadata may be unavailable for leaf or damaged images; retain the bounded frame-pointer fallback. */
    while (FrameCount < MaximumFrames)
    {
        if ((Frame < StackLow) || (Frame >= StackHigh) || ((StackHigh - Frame) < (2 * sizeof(ULONG_PTR))) || ((Frame & (sizeof(ULONG_PTR) - 1)) != 0))
            break;
        if (!MmIsAddressValid((PVOID)Frame) || !MmIsAddressValid((PVOID)(Frame + sizeof(ULONG_PTR))))
            break;

        NextFrame = *(volatile ULONG_PTR *)Frame;
        ReturnAddress = *(volatile ULONG_PTR *)(Frame + sizeof(ULONG_PTR));
        if (ReturnAddress == 0)
            break;
#if defined(_M_AMD64) || defined(_M_ARM64)
        if (!KiIsBugCheckCodeAddress(ReturnAddress))
            break;
#endif
        if (Frames[FrameCount - 1] != ReturnAddress)
            Frames[FrameCount++] = ReturnAddress;
        if ((NextFrame <= Frame) || (NextFrame < StackLow) || (NextFrame >= StackHigh))
            break;

        Frame = NextFrame;
    }

    return FrameCount;
}

static
VOID
KiDisplayBugCheckRegisters(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context)
{
    CHAR Line[128];

    KiDisplayAndLogBugCheckString("Registers:\r\n");

#if defined(_M_ARM64)
    {
        const ULONG64 *Registers;
        ULONG RegisterCount;
        ULONG Base;
        ULONG64 Pc;
        ULONG64 Lr;
        ULONG64 Sp;
        ULONG64 Fp;
        ULONG Cpsr;

        if (TrapFrame != NULL)
        {
            Registers = TrapFrame->X;
            RegisterCount = RTL_NUMBER_OF(TrapFrame->X);
            Pc = TrapFrame->Pc;
            Lr = TrapFrame->Lr;
            Sp = TrapFrame->Sp;
            Fp = TrapFrame->Fp;
            Cpsr = TrapFrame->Spsr;
        }
        else
        {
            Registers = Context->X;
            RegisterCount = 29;
            Pc = Context->Pc;
            Lr = Context->Lr;
            Sp = Context->Sp;
            Fp = Context->Fp;
            Cpsr = Context->Cpsr;
        }

        RtlStringCbPrintfA(Line, sizeof(Line), "PC=%016I64x LR=%016I64x SP=%016I64x\r\n", Pc, Lr, Sp);
        KiDisplayAndLogBugCheckString(Line);
        if (TrapFrame != NULL)
            RtlStringCbPrintfA(Line, sizeof(Line), "FP=%016I64x PSR=%08lx ESR=%08lx\r\n", Fp, Cpsr, TrapFrame->Esr);
        else
            RtlStringCbPrintfA(Line, sizeof(Line), "FP=%016I64x PSR=%08lx\r\n", Fp, Cpsr);
        KiDisplayAndLogBugCheckString(Line);

        for (Base = 0; Base < RegisterCount; Base += 3)
        {
            if ((RegisterCount - Base) >= 3)
                RtlStringCbPrintfA(Line, sizeof(Line), "X%02lu=%016I64x X%02lu=%016I64x X%02lu=%016I64x\r\n", Base, Registers[Base], Base + 1, Registers[Base + 1], Base + 2, Registers[Base + 2]);
            else if ((RegisterCount - Base) == 2)
                RtlStringCbPrintfA(Line, sizeof(Line), "X%02lu=%016I64x X%02lu=%016I64x\r\n", Base, Registers[Base], Base + 1, Registers[Base + 1]);
            else
                RtlStringCbPrintfA(Line, sizeof(Line), "X%02lu=%016I64x\r\n", Base, Registers[Base]);
            KiDisplayAndLogBugCheckString(Line);
        }
    }
#elif defined(_M_AMD64)
    if (TrapFrame != NULL)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "RIP=%016I64x RSP=%016I64x RBP=%016I64x EFL=%08lx\r\n", TrapFrame->Rip, TrapFrame->Rsp, TrapFrame->Rbp, TrapFrame->EFlags);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RAX=%016I64x RBX=%016I64x RCX=%016I64x\r\n", TrapFrame->Rax, TrapFrame->Rbx, TrapFrame->Rcx);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RDX=%016I64x RSI=%016I64x RDI=%016I64x\r\n", TrapFrame->Rdx, TrapFrame->Rsi, TrapFrame->Rdi);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R8 =%016I64x R9 =%016I64x R10=%016I64x\r\n", TrapFrame->R8, TrapFrame->R9, TrapFrame->R10);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R11=%016I64x FAR=%016I64x ERR=%016I64x\r\n", TrapFrame->R11, TrapFrame->FaultAddress, TrapFrame->ErrorCode);
        KiDisplayAndLogBugCheckString(Line);
    }
    else
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "RIP=%016I64x RSP=%016I64x RBP=%016I64x EFL=%08lx\r\n", Context->Rip, Context->Rsp, Context->Rbp, Context->EFlags);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RAX=%016I64x RBX=%016I64x RCX=%016I64x\r\n", Context->Rax, Context->Rbx, Context->Rcx);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RDX=%016I64x RSI=%016I64x RDI=%016I64x\r\n", Context->Rdx, Context->Rsi, Context->Rdi);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R8 =%016I64x R9 =%016I64x R10=%016I64x\r\n", Context->R8, Context->R9, Context->R10);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R11=%016I64x R12=%016I64x R13=%016I64x\r\n", Context->R11, Context->R12, Context->R13);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R14=%016I64x R15=%016I64x\r\n", Context->R14, Context->R15);
        KiDisplayAndLogBugCheckString(Line);
    }
#else
    RtlStringCbPrintfA(Line, sizeof(Line), "PC=%p\r\n", (PVOID)KeGetContextPc(Context));
    KiDisplayAndLogBugCheckString(Line);
#endif
}

static
VOID
KiFormatBugCheckFrame(
    _In_ ULONG Index,
    _In_ ULONG_PTR Frame,
    _Out_writes_bytes_(LineSize) PCHAR Line,
    _In_ SIZE_T LineSize)
{
    BOOLEAN InKernel;
    PVOID ImageBase;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    CHAR ModuleName[64];
#ifdef KDBG
    CHAR FunctionName[64];
    ULONG_PTR Displacement;

    if (KdbSymDescribeAddress((PVOID)Frame, ModuleName, sizeof(ModuleName), FunctionName, sizeof(FunctionName), &Displacement))
    {
        if (FunctionName[0] != ANSI_NULL)
            RtlStringCbPrintfA(Line, LineSize, "#%02lu %s!%s+0x%Ix\r\n", Index, ModuleName, FunctionName, Displacement);
        else
            RtlStringCbPrintfA(Line, LineSize, "#%02lu %s+0x%Ix\r\n", Index, ModuleName, Displacement);
        return;
    }
#endif

    ImageBase = NULL;
    LdrEntry = NULL;
    if (Frame > (ULONG_PTR)MmHighestUserAddress)
        ImageBase = KiPcToFileHeader((PVOID)Frame, &LdrEntry, FALSE, &InKernel);

    if ((ImageBase != NULL) && (LdrEntry != NULL))
    {
        KeBugCheckUnicodeToAnsi(&LdrEntry->BaseDllName, ModuleName, sizeof(ModuleName));
        RtlStringCbPrintfA(Line, LineSize, "#%02lu %s+0x%Ix\r\n", Index, ModuleName, Frame - (ULONG_PTR)ImageBase);
    }
    else
    {
        RtlStringCbPrintfA(Line, LineSize, "#%02lu %p\r\n", Index, (PVOID)Frame);
    }
}

static
VOID
KiDisplayBugCheckBackTrace(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context)
{
    ULONG_PTR Frames[KI_BUGCHECK_BACKTRACE_FRAMES];
    ULONG FrameCount;
    ULONG Index;
    CHAR Line[128];

    FrameCount = KiCaptureBugCheckBackTrace(TrapFrame, Context, KeGetCurrentThread(), Frames, RTL_NUMBER_OF(Frames));
    KiDisplayAndLogBugCheckString("Backtrace:\r\n");

    for (Index = 0; Index < FrameCount; Index++)
    {
        KiFormatBugCheckFrame(Index, Frames[Index], Line, sizeof(Line));
        KiDisplayAndLogBugCheckString(Line);
    }

    if (FrameCount == 0)
        KiDisplayAndLogBugCheckString("<unavailable>\r\n");
}

#ifdef CONFIG_SMP
static
VOID
KiLogProcessorBackTraces(
    _In_ PCONTEXT OwnerContext)
{
    PKPRCB CurrentPrcb;
    PKPRCB TargetPrcb;
    PKTHREAD Thread;
    PCONTEXT Context;
    ULONG_PTR Frames[KI_BUGCHECK_BACKTRACE_FRAMES];
    ULONG FrameCount;
    ULONG Processor;
    ULONG Index;
    CHAR Line[128];

    CurrentPrcb = KeGetCurrentPrcb();
    KiLogBugCheckString("\r\nSMP processor backtraces:\r\n");

    for (Processor = 0; Processor < KeNumberProcessors; Processor++)
    {
        TargetPrcb = KiProcessorBlock[Processor];
        if (TargetPrcb == NULL)
            continue;

        RtlStringCbPrintfA(Line, sizeof(Line), "CPU %lu%s:\r\n", Processor, TargetPrcb == CurrentPrcb ? " (bugcheck owner)" : "");
        KiLogBugCheckString(Line);

        if ((TargetPrcb != CurrentPrcb) && ((TargetPrcb->IpiFrozen & ~IPI_FROZEN_FLAG_ACTIVE) != IPI_FROZEN_STATE_FROZEN))
        {
            KiLogBugCheckString("<not frozen>\r\n");
            continue;
        }

        Context = TargetPrcb == CurrentPrcb ? OwnerContext : &TargetPrcb->ProcessorState.ContextFrame;
        Thread = TargetPrcb->CurrentThread;
        FrameCount = KiCaptureBugCheckBackTrace(NULL, Context, Thread, Frames, RTL_NUMBER_OF(Frames));
        if (FrameCount == 0)
        {
            KiLogBugCheckString("<unavailable>\r\n");
            continue;
        }

        for (Index = 0; Index < FrameCount; Index++)
        {
            KiFormatBugCheckFrame(Index, Frames[Index], Line, sizeof(Line));
            KiLogBugCheckString(Line);
        }
    }
}
#endif

static
BOOLEAN
KiUseExceptionBugCheckContext(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR ContextAddress,
    _Out_ PCONTEXT Context)
{
    CONTEXT ExceptionContext;
    ULONG_PTR ProgramCounter;
    BOOLEAN ContextCopied = FALSE;

    if ((BugCheckCode != SYSTEM_THREAD_EXCEPTION_NOT_HANDLED) ||
        (ContextAddress <= (ULONG_PTR)MmHighestUserAddress) ||
        (ContextAddress > MAXULONG_PTR - (sizeof(CONTEXT) - 1)) ||
        !MmIsAddressValid((PVOID)ContextAddress) ||
        !MmIsAddressValid((PVOID)(ContextAddress + sizeof(CONTEXT) - 1)))
    {
        return FALSE;
    }

    _SEH2_TRY
    {
        ExceptionContext = *(volatile CONTEXT *)ContextAddress;
        ContextCopied = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ContextCopied = FALSE;
    }
    _SEH2_END;

    if (!ContextCopied)
        return FALSE;

    ProgramCounter = KeGetContextPc(&ExceptionContext);
    if (ProgramCounter == 0)
        return FALSE;

#if defined(_M_AMD64) || defined(_M_ARM64)
    if (!KiIsBugCheckCodeAddress(ProgramCounter))
        return FALSE;
#else
    if (!MmIsAddressValid((PVOID)ProgramCounter))
        return FALSE;
#endif

    *Context = ExceptionContext;
    return TRUE;
}

VOID
NTAPI
KiDumpParameterImages(IN PCHAR Message,
                      IN PULONG_PTR Parameters,
                      IN ULONG ParameterCount,
                      IN PKE_BUGCHECK_UNICODE_TO_ANSI ConversionRoutine)
{
    ULONG i;
    BOOLEAN InSystem;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PVOID ImageBase;
    PUNICODE_STRING DriverName;
    CHAR AnsiName[32];
    PIMAGE_NT_HEADERS NtHeader;
    ULONG TimeStamp;
    BOOLEAN FirstRun = TRUE;

    /* Loop parameters */
    for (i = 0; i < ParameterCount; i++)
    {
        /* Get the base for this parameter */
        ImageBase = KiPcToFileHeader((PVOID)Parameters[i],
                                     &LdrEntry,
                                     FALSE,
                                     &InSystem);
        if (!ImageBase)
        {
            if ((Parameters[i] == 0) ||
                (Parameters[i] < (ULONG_PTR)MmSystemRangeStart))
            {
                continue;
            }

            /* FIXME: Add code to check for unloaded drivers */
            DPRINT1("Potentially unloaded driver!\n");
            continue;
        }
        else
        {
            /* Get the NT Headers and Timestamp */
            NtHeader = RtlImageNtHeader(LdrEntry->DllBase);
            TimeStamp = NtHeader->FileHeader.TimeDateStamp;

            /* Convert the driver name */
            DriverName = &LdrEntry->BaseDllName;
            ConversionRoutine(&LdrEntry->BaseDllName,
                              AnsiName,
                              sizeof(AnsiName));
        }

        /* Format driver name */
        sprintf(Message,
                "%s**  %12s - Address %p base at %p, DateStamp %08lx\r\n",
                FirstRun ? "\r\n*":"*",
                AnsiName,
                (PVOID)Parameters[i],
                ImageBase,
                TimeStamp);

        /* Check if we only had one parameter */
        if (ParameterCount <= 1)
        {
            /* Then just save the name */
            KiBugCheckDriver = DriverName;
        }
        else
        {
            /* Otherwise, display the message */
            InbvDisplayString(Message);
        }

        /* Loop again */
        FirstRun = FALSE;
    }
}

VOID
NTAPI
KiDisplayBlueScreen(IN ULONG MessageId,
                    IN BOOLEAN IsHardError,
                    IN PCHAR HardErrCaption OPTIONAL,
                    IN PCHAR HardErrMessage OPTIONAL,
                    IN PKTRAP_FRAME TrapFrame OPTIONAL,
                    IN PCONTEXT Context)
{
    ULONG BugCheckCode = (ULONG)KiBugCheckData[0];
    BOOLEAN Enable = TRUE;
    CHAR AnsiName[107];

    /* Enable headless support for bugcheck */
    HeadlessDispatch(HeadlessCmdStartBugCheck,
                     NULL, 0, NULL, NULL);
    HeadlessDispatch(HeadlessCmdEnableTerminal,
                     &Enable, sizeof(Enable),
                     NULL, NULL);
    HeadlessDispatch(HeadlessCmdSendBlueScreenData,
                     &BugCheckCode, sizeof(BugCheckCode),
                     NULL, NULL);

    /* Check if bootvid is installed */
    if (InbvIsBootDriverInstalled())
    {
        if (KiIsScreenDebuggingEnabled() &&
            (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED))
        {
            /*
             * Preserve the screen-debug log and its cursor. The crash report
             * will append through the existing bootvid scrolling path.
             */
            InbvSetTextColor(BV_COLOR_WHITE);
            InbvInstallDisplayStringFilter(NULL);
            InbvEnableDisplayString(TRUE);
        }
        else
        {
            /* Acquire ownership and reset the display */
            InbvAcquireDisplayOwnership();
            InbvResetDisplay();

            /* Display blue screen */
            InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLUE);
            InbvSetTextColor(BV_COLOR_WHITE);
            InbvInstallDisplayStringFilter(NULL);
            InbvEnableDisplayString(TRUE);
            InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
        }
    }

    /* Check if this is a hard error */
    if (IsHardError)
    {
        /* Display caption and message */
        if (HardErrCaption) InbvDisplayString(HardErrCaption);
        if (HardErrMessage) InbvDisplayString(HardErrMessage);
    }

    /* Begin the display */
    InbvDisplayString("\r\n");

    /* Print out initial message */
    KeGetBugMessageText(BUGCHECK_MESSAGE_INTRO, NULL);
    InbvDisplayString("\r\n\r\n");

    /* Check if we have a driver */
    if (KiBugCheckDriver)
    {
        /* Print out into to driver name */
        KeGetBugMessageText(BUGCODE_ID_DRIVER, NULL);

        /* Convert and print out driver name */
        KeBugCheckUnicodeToAnsi(KiBugCheckDriver, AnsiName, sizeof(AnsiName));
        InbvDisplayString(" ");
        InbvDisplayString(AnsiName);
        InbvDisplayString("\r\n\r\n");
    }

    /* Check if this is the generic message */
    if (MessageId == BUGCODE_PSS_MESSAGE)
    {
        /* It is, so get the bug code string as well */
        KeGetBugMessageText(BugCheckCode, NULL);
        InbvDisplayString("\r\n\r\n");
    }

    /* The legacy restart, BIOS, and Safe Mode advice wastes the crash screen. */
    InbvDisplayString("\r\n");

    /* Print message for technical information */
    KeGetBugMessageText(BUGCHECK_TECH_INFO, NULL);

    /* Show the technical Data */
    RtlStringCbPrintfA(AnsiName,
                       sizeof(AnsiName),
                       "\r\n\r\n*** STOP: 0x%08lX (0x%p,0x%p,0x%p,0x%p)\r\n\r\n",
                       BugCheckCode,
                       (PVOID)KiBugCheckData[1],
                       (PVOID)KiBugCheckData[2],
                       (PVOID)KiBugCheckData[3],
                       (PVOID)KiBugCheckData[4]);
    KiDisplayAndLogBugCheckString(AnsiName);

    KiDisplayBugCheckRegisters(TrapFrame, Context);
    KiDisplayBugCheckBackTrace(TrapFrame, Context);
}

DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheckWithTf(IN ULONG BugCheckCode,
                 IN ULONG_PTR BugCheckParameter1,
                 IN ULONG_PTR BugCheckParameter2,
                 IN ULONG_PTR BugCheckParameter3,
                 IN ULONG_PTR BugCheckParameter4,
                 IN PKTRAP_FRAME TrapFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    CONTEXT Context;
    ULONG MessageId;
    CHAR AnsiName[128];
    BOOLEAN IsSystem, IsHardError = FALSE, Reboot = FALSE;
    BOOLEAN IsDoubleFault;
    PCHAR HardErrCaption = NULL, HardErrMessage = NULL;
    PVOID Pc = NULL, Memory;
    PVOID DriverBase;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PULONG_PTR HardErrorParameters;
    NTSTATUS DumpStatus;
    KIRQL OldIrql;

    IsDoubleFault = (BugCheckCode == UNEXPECTED_KERNEL_MODE_TRAP) && (BugCheckParameter1 == EXCEPTION_DOUBLE_FAULT);

    /* Set active bugcheck */
    KeBugCheckActive = TRUE;
    KiBugCheckDriver = NULL;

    /* Check if this is power failure simulation */
    if (BugCheckCode == POWER_FAILURE_SIMULATE)
    {
        /* Call the Callbacks and reboot */
        KiDoBugCheckCallbacks();
        HalReturnToFirmware(HalRebootRoutine);
    }

    /* Save the IRQL and set hardware trigger */
    Prcb->DebuggerSavedIRQL = KeGetCurrentIrql();
    InterlockedIncrement((PLONG)&KiHardwareTrigger);

    /* Capture the CPU Context */
    RtlCaptureContext(&Prcb->ProcessorState.ContextFrame);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    Context = Prcb->ProcessorState.ContextFrame;

    /* 0x7E parameter 4 is the context at the original exception. */
    if (KiUseExceptionBugCheckContext(BugCheckCode, BugCheckParameter4, &Context))
    {
        TrapFrame = NULL;
        Prcb->ProcessorState.ContextFrame = Context;
    }

    /* FIXME: Call the Watchdog if it's registered */

    /* Check which bugcode this is */
    switch (BugCheckCode)
    {
        /* These bug checks already have detailed messages, keep them */
        case UNEXPECTED_KERNEL_MODE_TRAP:
        case DRIVER_CORRUPTED_EXPOOL:
        case ACPI_BIOS_ERROR:
        case ACPI_BIOS_FATAL_ERROR:
        case THREAD_STUCK_IN_DEVICE_DRIVER:
        case DATA_BUS_ERROR:
        case FAT_FILE_SYSTEM:
        case NO_MORE_SYSTEM_PTES:
        case INACCESSIBLE_BOOT_DEVICE:

            /* Keep the same code */
            MessageId = BugCheckCode;
            break;

        /* Check if this is a kernel-mode exception */
        case KERNEL_MODE_EXCEPTION_NOT_HANDLED:
        case SYSTEM_THREAD_EXCEPTION_NOT_HANDLED:
        case KMODE_EXCEPTION_NOT_HANDLED:

            /* Use the generic text message */
            MessageId = KMODE_EXCEPTION_NOT_HANDLED;
            break;

        /* File-system errors */
        case NTFS_FILE_SYSTEM:

            /* Use the generic message for FAT */
            MessageId = FAT_FILE_SYSTEM;
            break;

        /* Check if this is a coruption of the Mm's Pool */
        case DRIVER_CORRUPTED_MMPOOL:

            /* Use generic corruption message */
            MessageId = DRIVER_CORRUPTED_EXPOOL;
            break;

        /* Check if this is a signature check failure */
        case STATUS_SYSTEM_IMAGE_BAD_SIGNATURE:

            /* Use the generic corruption message */
            MessageId = BUGCODE_PSS_MESSAGE_SIGNATURE;
            break;

        /* All other codes */
        default:

            /* Use the default bugcheck message */
            MessageId = BUGCODE_PSS_MESSAGE;
            break;
    }

    /* Save bugcheck data */
    KiBugCheckData[0] = BugCheckCode;
    KiBugCheckData[1] = BugCheckParameter1;
    KiBugCheckData[2] = BugCheckParameter2;
    KiBugCheckData[3] = BugCheckParameter3;
    KiBugCheckData[4] = BugCheckParameter4;

    /* Now check what bugcheck this is */
    switch (BugCheckCode)
    {
        case SYSTEM_THREAD_EXCEPTION_NOT_HANDLED:
            Pc = (PVOID)BugCheckParameter2;
            break;

        /* Invalid access to R/O memory or Unhandled KM Exception */
        case KERNEL_MODE_EXCEPTION_NOT_HANDLED:
        case ATTEMPTED_WRITE_TO_READONLY_MEMORY:
        case ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY:
        {
            /* Check if we have a trap frame */
            if (!TrapFrame)
            {
                /* Use parameter 3 as a trap frame, if it exists */
                if (BugCheckParameter3) TrapFrame = (PVOID)BugCheckParameter3;
            }

            /* Check if we got one now and if we need to get the Program Counter */
            if ((TrapFrame) &&
                (BugCheckCode != KERNEL_MODE_EXCEPTION_NOT_HANDLED))
            {
                /* Get the Program Counter */
                Pc = (PVOID)KeGetTrapFramePc(TrapFrame);
            }
            break;
        }

        /* Wrong IRQL */
        case IRQL_NOT_LESS_OR_EQUAL:
        {
            /*
             * The NT kernel has 3 special sections:
             * MISYSPTE, POOLMI and POOLCODE. The bug check code can
             * determine in which of these sections this bugcode happened
             * and provide a more detailed analysis. For now, we don't.
             */

            /* Program Counter is in parameter 4 */
            Pc = (PVOID)BugCheckParameter4;

            /* Get the driver base */
            DriverBase = KiPcToFileHeader(Pc,
                                          &LdrEntry,
                                          FALSE,
                                          &IsSystem);
            if (IsSystem)
            {
                /*
                 * The error happened inside the kernel or HAL.
                 * Get the memory address that was being referenced.
                 */
                Memory = (PVOID)BugCheckParameter1;

                /* Find to which driver it belongs */
                DriverBase = KiPcToFileHeader(Memory,
                                              &LdrEntry,
                                              TRUE,
                                              &IsSystem);
                if (DriverBase)
                {
                    /* Get the driver name and update the bug code */
                    KiBugCheckDriver = &LdrEntry->BaseDllName;
                    KiBugCheckData[0] = DRIVER_PORTION_MUST_BE_NONPAGED;
                }
                else
                {
                    /* Find the driver that unloaded at this address */
                    KiBugCheckDriver = NULL; // FIXME: ROS can't locate

                    /* Check if the cause was an unloaded driver */
                    if (KiBugCheckDriver)
                    {
                        /* Update bug check code */
                        KiBugCheckData[0] =
                            SYSTEM_SCAN_AT_RAISED_IRQL_CAUGHT_IMPROPER_DRIVER_UNLOAD;
                    }
                }
            }
            else
            {
                /* Update the bug check code */
                KiBugCheckData[0] = DRIVER_IRQL_NOT_LESS_OR_EQUAL;
            }

            /* Clear Pc so we don't look it up later */
            Pc = NULL;
            break;
        }

        /* Hard error */
        case FATAL_UNHANDLED_HARD_ERROR:
        {
            /* Copy bug check data from hard error */
            HardErrorParameters = (PULONG_PTR)BugCheckParameter2;
            KiBugCheckData[0] = BugCheckParameter1;
            KiBugCheckData[1] = HardErrorParameters[0];
            KiBugCheckData[2] = HardErrorParameters[1];
            KiBugCheckData[3] = HardErrorParameters[2];
            KiBugCheckData[4] = HardErrorParameters[3];

            /* Remember that this is hard error and set the caption/message */
            IsHardError = TRUE;
            HardErrCaption = (PCHAR)BugCheckParameter3;
            HardErrMessage = (PCHAR)BugCheckParameter4;
            break;
        }

        /* Page fault */
        case PAGE_FAULT_IN_NONPAGED_AREA:
        {
            /* Assume no driver */
            DriverBase = NULL;

            /* Check if we have a trap frame */
            if (!TrapFrame)
            {
                /* We don't, use parameter 3 if possible */
                if (BugCheckParameter3) TrapFrame = (PVOID)BugCheckParameter3;
            }

            /* Check if we have a frame now */
            if (TrapFrame)
            {
                /* Get the Program Counter */
                Pc = (PVOID)KeGetTrapFramePc(TrapFrame);
                KiBugCheckData[3] = (ULONG_PTR)Pc;

                /* Find out if was in the kernel or drivers */
                DriverBase = KiPcToFileHeader(Pc,
                                              &LdrEntry,
                                              FALSE,
                                              &IsSystem);
            }
            else
            {
                /* Can't blame a driver, assume system */
                IsSystem = TRUE;
            }

            /* FIXME: Check for session pool in addition to special pool */

            /* Special pool has its own bug check codes */
            if (MmIsSpecialPoolAddress((PVOID)BugCheckParameter1))
            {
                if (MmIsSpecialPoolAddressFree((PVOID)BugCheckParameter1))
                {
                    KiBugCheckData[0] = IsSystem
                        ? PAGE_FAULT_IN_FREED_SPECIAL_POOL
                        : DRIVER_PAGE_FAULT_IN_FREED_SPECIAL_POOL;
                }
                else
                {
                    KiBugCheckData[0] = IsSystem
                        ? PAGE_FAULT_BEYOND_END_OF_ALLOCATION
                        : DRIVER_PAGE_FAULT_BEYOND_END_OF_ALLOCATION;
                }
            }
            else if (!DriverBase)
            {
                /* Find the driver that unloaded at this address */
                KiBugCheckDriver = NULL; // FIXME: ROS can't locate

                /* Check if the cause was an unloaded driver */
                if (KiBugCheckDriver)
                {
                    KiBugCheckData[0] =
                        DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS;
                }
            }
            break;
        }

        /* Check if the driver forgot to unlock pages */
        case DRIVER_LEFT_LOCKED_PAGES_IN_PROCESS:

            /* Program Counter is in parameter 1 */
            Pc = (PVOID)BugCheckParameter1;
            break;

        /* Check if the driver consumed too many PTEs */
        case DRIVER_USED_EXCESSIVE_PTES:

            /* Loader entry is in parameter 1 */
            LdrEntry = (PVOID)BugCheckParameter1;
            KiBugCheckDriver = &LdrEntry->BaseDllName;
            break;

        /* Check if the driver has a stuck thread */
        case THREAD_STUCK_IN_DEVICE_DRIVER:

            /* The name is in Parameter 3 */
            KiBugCheckDriver = (PVOID)BugCheckParameter3;
            break;

        /* Anything else */
        default:
            break;
    }

    /* Do we have a driver name? */
    if (KiBugCheckDriver)
    {
        /* Convert it to ANSI */
        KeBugCheckUnicodeToAnsi(KiBugCheckDriver, AnsiName, sizeof(AnsiName));
    }
    else
    {
        /* Do we have a Program Counter? */
        if (Pc)
        {
            /* Dump image name */
            KiDumpParameterImages(AnsiName,
                                  (PULONG_PTR)&Pc,
                                  1,
                                  KeBugCheckUnicodeToAnsi);
        }
    }

    /* Check if we need to save the context for KD */
    if (!KdPitchDebugger) KdDebuggerDataBlock.SavedContext = (ULONG_PTR)&Context;

    /* Check if a debugger is connected */
    if ((BugCheckCode != MANUALLY_INITIATED_CRASH) && (KdDebuggerEnabled))
    {
        /* Crash on the debugger console */
        DbgPrint("\n*** Fatal System Error: 0x%08lx\n"
                 "                       (0x%p,0x%p,0x%p,0x%p)\n\n",
                 KiBugCheckData[0],
                 KiBugCheckData[1],
                 KiBugCheckData[2],
                 KiBugCheckData[3],
                 KiBugCheckData[4]);

        /* Check if the debugger isn't currently connected */
        if (!KdDebuggerNotPresent && !IsDoubleFault)
        {
            /* Check if we have a driver to blame */
            if (KiBugCheckDriver)
            {
                /* Dump it */
                DbgPrint("Driver at fault: %s.\n", AnsiName);
            }

            /* Check if this was a hard error */
            if (IsHardError)
            {
                /* Print caption and message */
                if (HardErrCaption) DbgPrint(HardErrCaption);
                if (HardErrMessage) DbgPrint(HardErrMessage);
            }

            /* Break in the debugger */
            KiBugCheckDebugBreak(DBG_STATUS_BUGCHECK_FIRST);
        }
    }

    /* Raise IRQL to HIGH_LEVEL */
    _disable();
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

    /* Avoid recursion */
    if (!InterlockedDecrement((PLONG)&KeBugCheckCount))
    {
#ifdef CONFIG_SMP
        /* Set CPU that is bug checking now */
        KeBugCheckOwner = Prcb->Number;

        /* Freeze the other CPUs */
        KxFreezeExecution();
#endif

        /* Display the BSOD */
        KiDisplayBlueScreen(MessageId, IsHardError, HardErrCaption, HardErrMessage, TrapFrame, &Context);

#ifdef CONFIG_SMP
        /* Frozen PRCB contexts are stable now; preserve every CPU trace in the crash-log tail. */
        KiLogProcessorBackTraces(&Context);
#endif

        // TODO/FIXME: Run the registered reason-callbacks from
        // the KeBugcheckReasonCallbackListHead list with the
        // KbCallbackReserved1 reason.

#if defined(_M_ARM64)
        {
            static const CHAR Hex[] = "0123456789ABCDEF";
            volatile UCHAR *Uart = (volatile UCHAR *)0xFFFFFC0009000000ULL;
            ULONG Index;
            ULONG Shift;
            ULONG_PTR Values[5];

            Values[0] = KiBugCheckData[0];
            Values[1] = KiBugCheckData[1];
            Values[2] = KiBugCheckData[2];
            Values[3] = KiBugCheckData[3];
            Values[4] = KiBugCheckData[4];

            for (const CHAR *Text = "[arm64][bugcheck] "; *Text; ++Text) *Uart = *Text;
            for (Index = 0; Index < RTL_NUMBER_OF(Values); ++Index)
            {
                if (Index != 0) *Uart = ' ';
                for (Shift = (sizeof(ULONG_PTR) * 8) - 4;
                     Shift < (sizeof(ULONG_PTR) * 8);
                     Shift -= 4)
                {
                    *Uart = Hex[(Values[Index] >> Shift) & 0xF];
                }
            }
            *Uart = '\n';
        }
#endif

        /* Re-enable an initialized debugger, but never probe a new transport after drawing the crash report. */
        if (!(KdDebuggerEnabled) && !(KdPitchDebugger) && KdPreviouslyEnabled)
        {
            /* Enable it */
            KdEnableDebuggerWithLock(FALSE);
        }
        else
        {
            /* Otherwise, print the last line */
            InbvDisplayString("\r\n");
        }

        /* Save the context */
        Prcb->ProcessorState.ContextFrame = Context;

        /* FIXME: Support Triage Dump */

        /*
         * The crash target, physical file layout, and transport resources
         * were prepared before SMSS starts. The other processors are frozen
         * now, and this path does not depend on filesystem or paging I/O.
         */
        DumpStatus = KdpWriteCrashDump();
        if (!NT_SUCCESS(DumpStatus)) DbgPrint("KD: Crash dump writer returned 0x%08lx.\n", DumpStatus);

        // TODO: The crash-dump helper must set the Reboot variable.
        Reboot = !!IopAutoReboot;
    }
    else
    {
        /* Increase recursion count */
        KeBugCheckOwnerRecursionCount++;
        if (KeBugCheckOwnerRecursionCount == 2)
        {
            /* Break in the debugger */
            KiBugCheckDebugBreak(DBG_STATUS_BUGCHECK_SECOND);
        }
        else if (KeBugCheckOwnerRecursionCount > 2)
        {
            /* Halt execution */
            while (TRUE);
        }
    }

    /* Call the Callbacks */
    KiDoBugCheckCallbacks();

    /* FIXME: Call Watchdog if enabled */

    /* Check if we have to reboot */
    if (Reboot)
    {
        /* Unload symbols */
        DbgUnLoadImageSymbols(NULL, (PVOID)MAXULONG_PTR, 0);
        HalReturnToFirmware(HalRebootRoutine);
    }

    /* A double fault is already using the emergency IST. Do not recursively
     * enter the debugger after the crash report and dump are complete. */
    if (IsDoubleFault) HalHaltSystem();

    /* Attempt to break in the debugger (otherwise halt CPU) */
    KiBugCheckDebugBreak(DBG_STATUS_BUGCHECK_SECOND);

    /* Shouldn't get here */
    ASSERT(FALSE);
    while (TRUE);
}

BOOLEAN
NTAPI
KiHandleNmi(VOID)
{
    BOOLEAN Handled = FALSE;
    PKNMI_HANDLER_CALLBACK NmiData;

    /* Parse the list of callbacks */
    NmiData = KiNmiCallbackListHead;
    while (NmiData)
    {
        /* Save if this callback has handled it -- all it takes is one */
        Handled |= NmiData->Callback(NmiData->Context, Handled);
        NmiData = NmiData->Next;
    }

    /* Has anyone handled this? */
    return Handled;
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
KeInitializeCrashDumpHeader(IN ULONG Type, IN ULONG Flags, OUT PVOID Buffer, IN ULONG BufferSize, OUT PULONG BufferNeeded OPTIONAL)
{
#if defined(_M_AMD64) || defined(_M_ARM64)
    PDUMP_HEADER64 Header;
    ULONG Index;
    SIZE_T DescriptorSize;

    if (BufferNeeded)
        *BufferNeeded = DUMP_HEADER64_SIZE;

    if (Type != DUMP_TYPE_FULL)
        return STATUS_INVALID_PARAMETER_1;

    if (Flags != 0)
        return STATUS_INVALID_PARAMETER_2;

    if (!Buffer)
        return STATUS_INVALID_PARAMETER_3;

    if (BufferSize < DUMP_HEADER64_SIZE)
        return STATUS_INVALID_PARAMETER_4;

    if (!MmPhysicalMemoryBlock)
        return STATUS_UNSUCCESSFUL;

    DescriptorSize = FIELD_OFFSET(DUMP_PHYSICAL_MEMORY_DESCRIPTOR64, Run[MmPhysicalMemoryBlock->NumberOfRuns]);
    if (DescriptorSize > DUMP_PHYSICAL_MEMORY_BLOCK_SIZE64)
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(Buffer, DUMP_HEADER64_SIZE);
    Header = Buffer;

    Header->Signature = DUMP_SIGNATURE64;
    Header->ValidDump = DUMP_VALID_DUMP64;
    Header->MajorVersion = KdVersionBlock.MajorVersion;
    Header->MinorVersion = KdVersionBlock.MinorVersion;
    if (PsInitialSystemProcess != NULL)
    {
        Header->DirectoryTableBase = KPROCESS_DTB0(&PsInitialSystemProcess->Pcb);
    }
#if defined(_M_ARM64)
    else
    {
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Header->DirectoryTableBase));
    }
#else
    else
    {
        Header->DirectoryTableBase = __readcr3();
    }
#endif
    Header->PfnDataBase = (ULONG64)(ULONG_PTR)MmPfnDatabase;
    Header->PsLoadedModuleList = (ULONG64)(ULONG_PTR)&PsLoadedModuleList;
    Header->PsActiveProcessHead = (ULONG64)(ULONG_PTR)&PsActiveProcessHead;
#if defined(_M_ARM64)
    Header->MachineImageType = IMAGE_FILE_MACHINE_ARM64;
#else
    Header->MachineImageType = IMAGE_FILE_MACHINE_AMD64;
#endif
    Header->NumberProcessors = KeNumberProcessors;
    Header->BugCheckCode = (ULONG)KiBugCheckData[0];
    Header->BugCheckParameter1 = KiBugCheckData[1];
    Header->BugCheckParameter2 = KiBugCheckData[2];
    Header->BugCheckParameter3 = KiBugCheckData[3];
    Header->BugCheckParameter4 = KiBugCheckData[4];
    RtlCopyMemory(Header->VersionUser, "ReactOS", sizeof("ReactOS"));
    Header->KdDebuggerDataBlock = (ULONG64)(ULONG_PTR)&KdDebuggerDataBlock;

    Header->PhysicalMemoryBlock.NumberOfRuns = MmPhysicalMemoryBlock->NumberOfRuns;
    Header->PhysicalMemoryBlock.NumberOfPages = MmPhysicalMemoryBlock->NumberOfPages;
    for (Index = 0; Index < MmPhysicalMemoryBlock->NumberOfRuns; Index++)
    {
        Header->PhysicalMemoryBlock.Run[Index].BasePage = MmPhysicalMemoryBlock->Run[Index].BasePage;
        Header->PhysicalMemoryBlock.Run[Index].PageCount = MmPhysicalMemoryBlock->Run[Index].PageCount;
    }

    RtlCopyMemory(Header->ContextRecord, &KeGetCurrentPrcb()->ProcessorState.ContextFrame, sizeof(CONTEXT));
    Header->DumpType = DUMP_TYPE_FULL;
    Header->RequiredDumpSpace.QuadPart = DUMP_HEADER64_SIZE + (MmPhysicalMemoryBlock->NumberOfPages << PAGE_SHIFT);
    KeQuerySystemTime(&Header->SystemTime);
    Header->SystemUpTime.QuadPart = KeQueryInterruptTime();
    Header->ProductType = SharedUserData->NtProductType;
    Header->SuiteMask = SharedUserData->SuiteMask;
    Header->WriterStatus = STATUS_SUCCESS;
    Header->KdSecondaryVersion = KdVersionBlock.KdSecondaryVersion;
    return STATUS_SUCCESS;
#else
    if (BufferNeeded)
        *BufferNeeded = DUMP_HEADER32_SIZE;

    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferSize);
    return STATUS_NOT_SUPPORTED;
#endif
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeDeregisterBugCheckCallback(IN PKBUGCHECK_CALLBACK_RECORD CallbackRecord)
{
    KIRQL OldIrql;
    BOOLEAN Status = FALSE;

    /* Raise IRQL to High */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

    /* Check the Current State */
    if (CallbackRecord->State == BufferInserted)
    {
        /* Reset state and remove from list */
        CallbackRecord->State = BufferEmpty;
        RemoveEntryList(&CallbackRecord->Entry);
        Status = TRUE;
    }

    /* Lower IRQL and return */
    KeLowerIrql(OldIrql);
    return Status;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeDeregisterBugCheckReasonCallback(
    IN PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord)
{
    KIRQL OldIrql;
    BOOLEAN Status = FALSE;

    /* Raise IRQL to High */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

    /* Check the Current State */
    if (CallbackRecord->State == BufferInserted)
    {
        /* Reset state and remove from list */
        CallbackRecord->State = BufferEmpty;
        RemoveEntryList(&CallbackRecord->Entry);
        Status = TRUE;
    }

    /* Lower IRQL and return */
    KeLowerIrql(OldIrql);
    return Status;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeRegisterBugCheckCallback(IN PKBUGCHECK_CALLBACK_RECORD CallbackRecord,
                           IN PKBUGCHECK_CALLBACK_ROUTINE CallbackRoutine,
                           IN PVOID Buffer,
                           IN ULONG Length,
                           IN PUCHAR Component)
{
    KIRQL OldIrql;
    BOOLEAN Status = FALSE;

    /* Raise IRQL to High */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

    /* Check the Current State first so we don't double-register */
    if (CallbackRecord->State == BufferEmpty)
    {
        /* Set the Callback Settings and insert into the list */
        CallbackRecord->Length = Length;
        CallbackRecord->Buffer = Buffer;
        CallbackRecord->Component = Component;
        CallbackRecord->CallbackRoutine = CallbackRoutine;
        CallbackRecord->State = BufferInserted;
        InsertTailList(&KeBugcheckCallbackListHead, &CallbackRecord->Entry);
        Status = TRUE;
    }

    /* Lower IRQL and return */
    KeLowerIrql(OldIrql);
    return Status;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeRegisterBugCheckReasonCallback(
    IN PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord,
    IN PKBUGCHECK_REASON_CALLBACK_ROUTINE CallbackRoutine,
    IN KBUGCHECK_CALLBACK_REASON Reason,
    IN PUCHAR Component)
{
    KIRQL OldIrql;
    BOOLEAN Status = FALSE;

    /* Raise IRQL to High */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

    /* Check the Current State first so we don't double-register */
    if (CallbackRecord->State == BufferEmpty)
    {
        /* Set the Callback Settings and insert into the list */
        CallbackRecord->Component = Component;
        CallbackRecord->CallbackRoutine = CallbackRoutine;
        CallbackRecord->State = BufferInserted;
        CallbackRecord->Reason = Reason;
        InsertTailList(&KeBugcheckReasonCallbackListHead,
                       &CallbackRecord->Entry);
        Status = TRUE;
    }

    /* Lower IRQL and return */
    KeLowerIrql(OldIrql);
    return Status;
}

/*
 * @implemented
 */
PVOID
NTAPI
KeRegisterNmiCallback(IN PNMI_CALLBACK CallbackRoutine,
                      IN PVOID Context)
{
    KIRQL OldIrql;
    PKNMI_HANDLER_CALLBACK NmiData, Next;
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Allocate NMI callback data */
    NmiData = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NmiData), TAG_KNMI);
    if (!NmiData) return NULL;

    /* Fill in the information */
    NmiData->Callback = CallbackRoutine;
    NmiData->Context = Context;
    NmiData->Handle = NmiData;

    /* Insert it into NMI callback list */
    KiAcquireNmiListLock(&OldIrql);
    NmiData->Next = KiNmiCallbackListHead;
    Next = InterlockedCompareExchangePointer((PVOID*)&KiNmiCallbackListHead,
                                             NmiData,
                                             NmiData->Next);
    ASSERT(Next == NmiData->Next);
    KiReleaseNmiListLock(OldIrql);

    /* Return the opaque "handle" */
    return NmiData->Handle;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeDeregisterNmiCallback(IN PVOID Handle)
{
    KIRQL OldIrql;
    PKNMI_HANDLER_CALLBACK NmiData;
    PKNMI_HANDLER_CALLBACK* Previous;
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Find in the list the NMI callback corresponding to the handle */
    KiAcquireNmiListLock(&OldIrql);
    Previous = &KiNmiCallbackListHead;
    NmiData = *Previous;
    while (NmiData)
    {
        if (NmiData->Handle == Handle)
        {
            /* The handle is the pointer to the callback itself */
            ASSERT(Handle == NmiData);

            /* Found it, remove from the list */
            *Previous = NmiData->Next;
            break;
        }

        /* Not found; try again */
        Previous = &NmiData->Next;
        NmiData = *Previous;
    }
    KiReleaseNmiListLock(OldIrql);

    /* If we have found the entry, free it */
    if (NmiData)
    {
        ExFreePoolWithTag(NmiData, TAG_KNMI);
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_HANDLE;
}

/*
 * @implemented
 */
DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheckEx(IN ULONG BugCheckCode,
             IN ULONG_PTR BugCheckParameter1,
             IN ULONG_PTR BugCheckParameter2,
             IN ULONG_PTR BugCheckParameter3,
             IN ULONG_PTR BugCheckParameter4)
{
    /* Call the internal API */
    KeBugCheckWithTf(BugCheckCode,
                     BugCheckParameter1,
                     BugCheckParameter2,
                     BugCheckParameter3,
                     BugCheckParameter4,
                     NULL);
}

/*
 * @implemented
 */
DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheck(ULONG BugCheckCode)
{
    /* Call the internal API */
    KeBugCheckWithTf(BugCheckCode, 0, 0, 0, 0, NULL);
}

/*
 * @implemented
 */
VOID
NTAPI
KeEnterKernelDebugger(VOID)
{
    /* Disable interrupts */
    KiHardwareTrigger = 1;
    _disable();

    /* Check the bugcheck count */
    if (!InterlockedDecrement((PLONG)&KeBugCheckCount))
    {
        /* There was only one, is the debugger disabled? */
        if (!(KdDebuggerEnabled) && !(KdPitchDebugger))
        {
            /* Enable the debugger */
            KdInitSystem(0, NULL);
        }
    }

    /* Break in the debugger */
    KiBugCheckDebugBreak(DBG_STATUS_FATAL);
}

/* EOF */
