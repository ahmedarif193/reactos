/*
 * PROJECT:     ReactOS runtime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Basic unwind helpers needed for ARM64 user-mode runtime
 */

#include <rtl.h>

#ifndef PUNWIND_HISTORY_TABLE
typedef struct _UNWIND_HISTORY_TABLE UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;
#endif

#define NDEBUG
#include <debug.h>

#define ARM64_UNWIND_FLAG_MASK          0x3u
#define ARM64_UNWIND_FUNCTION_LENGTH_MASK 0x1ffcu

static __inline ULONG
RtlpArm64FunctionLength(_In_ const RUNTIME_FUNCTION *FunctionEntry)
{
    ULONG LengthField;

    LengthField = (FunctionEntry->UnwindData & ARM64_UNWIND_FUNCTION_LENGTH_MASK) >> 2;

    /* Length is encoded in 4-byte units. */
    return LengthField << 2;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Out_ PULONG Length)
{
    PVOID ExceptionDirectory;
    ULONG DirectorySize;

    if (!RtlPcToFileHeader((PVOID)ControlPc, (PVOID *)ImageBase))
    {
        return NULL;
    }

    ExceptionDirectory = RtlImageDirectoryEntryToData((PVOID)*ImageBase,
                                                      TRUE,
                                                      IMAGE_DIRECTORY_ENTRY_EXCEPTION,
                                                      &DirectorySize);
    if (ExceptionDirectory == NULL)
    {
        return NULL;
    }

    *Length = DirectorySize / sizeof(RUNTIME_FUNCTION);
    return (PRUNTIME_FUNCTION)ExceptionDirectory;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    PRUNTIME_FUNCTION FunctionTable;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG TableLength;
    ULONG IndexLo, IndexHi, IndexMid;

    FunctionTable = RtlLookupFunctionTable(ControlPc, ImageBase, &TableLength);
    if (FunctionTable == NULL)
    {
        UNREFERENCED_PARAMETER(HistoryTable);
        return NULL;
    }

    ControlPc -= *ImageBase;

    IndexLo = 0;
    IndexHi = TableLength;
    while (IndexHi > IndexLo)
    {
        ULONG FunctionEnd;

        IndexMid = (IndexLo + IndexHi) / 2;
        FunctionEntry = &FunctionTable[IndexMid];

        FunctionEnd = FunctionEntry->BeginAddress + RtlpArm64FunctionLength(FunctionEntry);

        if (ControlPc < FunctionEntry->BeginAddress)
        {
            IndexHi = IndexMid;
        }
        else if (ControlPc >= FunctionEnd)
        {
            IndexLo = IndexMid + 1;
        }
        else
        {
            return FunctionEntry;
        }
    }

    return NULL;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PVOID *HandlerData,
    _Out_ PDWORD64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    ULONG Flag = 0;
    ULONG FrameSize;
    ULONG RegI, RegF, Cr, Home;
    ULONG64 FrameBytes;
    ULONG64 FrameBase;
    ULONG Offset;
    ULONG64 SavedFp = ContextRecord->Fp;
    ULONG64 SavedLr = ContextRecord->Lr;

    UNREFERENCED_PARAMETER(HandlerType);
    UNREFERENCED_PARAMETER(ImageBase);
    UNREFERENCED_PARAMETER(ControlPc);

    if (HandlerData)
        *HandlerData = NULL;
    if (ContextPointers)
        RtlZeroMemory(ContextPointers, sizeof(*ContextPointers));

    FrameBase = ContextRecord->Sp;
    FrameBytes = 0;
    FrameSize = 0;
    RegI = RegF = Cr = Home = 0;

    if (FunctionEntry != NULL)
    {
        ULONG UnwindData = FunctionEntry->UnwindData;

        Flag = UnwindData & ARM64_UNWIND_FLAG_MASK;
        FrameSize = (UnwindData >> 13) & 0x1ff;
        RegI = (UnwindData >> 25) & 0xf;
        RegF = (UnwindData >> 29) & 0x7;
        Cr = (UnwindData >> 22) & 0x3;
        Home = (UnwindData >> 24) & 0x1;
    }

    if (RegF)
    {
        RegF += 1;
    }

    FrameBytes = (ULONG64)FrameSize * 16;

    if (Flag != 0 && FrameBytes != 0)
    {
        ULONG SaveArea;

        Offset = 0;
        SaveArea = 0;

        if (RegI)
        {
            SaveArea += RegI * 8;
        }
        if (Cr == 1)
        {
            SaveArea += 8;
        }
        if (RegF)
        {
            SaveArea += RegF * 8;
        }
        if (Home)
        {
            SaveArea += 8 * 8;
        }

        SaveArea = (SaveArea + 0xF) & ~0xF;

        /* Restore integer non-volatiles */
        for (ULONG Index = 0; Index < RegI; Index++)
        {
            ContextRecord->X[19 + Index] = *(ULONG64 *)(FrameBase + Offset);
            Offset += sizeof(ULONG64);
        }

        if (Cr == 1)
        {
            SavedLr = *(ULONG64 *)(FrameBase + Offset);
            Offset += sizeof(ULONG64);
        }

        for (ULONG Index = 0; Index < RegF; Index++)
        {
            ContextRecord->V[8 + Index].Low = *(ULONG64 *)(FrameBase + Offset);
            ContextRecord->V[8 + Index].High = 0;
            Offset += sizeof(ULONG64);
        }

        Offset += Home * 8 * 8;

        if (FrameBytes >= 16)
        {
            ULONG64 Pair = FrameBase + FrameBytes - 16;
            SavedFp = *(ULONG64 *)(Pair);
            SavedLr = *(ULONG64 *)(Pair + 8);
        }

        ContextRecord->Sp = FrameBase + FrameBytes;
    }
    else if (ContextRecord->Fp != 0)
    {
        SavedFp = *(ULONG64 *)(ContextRecord->Fp);
        SavedLr = *(ULONG64 *)(ContextRecord->Fp + sizeof(ULONG64));
        ContextRecord->Sp = ContextRecord->Fp + 2 * sizeof(ULONG64);
    }
    else
    {
        SavedLr = *(ULONG64 *)(ContextRecord->Sp);
        ContextRecord->Sp += sizeof(ULONG64);
    }

    ContextRecord->Fp = SavedFp;
    ContextRecord->Lr = SavedLr;
    ContextRecord->Pc = SavedLr;

    if (EstablisherFrame)
    {
        *EstablisherFrame = ContextRecord->Sp;
    }

    return NULL;
}
