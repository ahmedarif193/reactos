/* Deterministic filtered exports and cross-session comparison. */

#include "profiler_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VOID
RperfExportName(const RPERF_FUNCTION_COST *Function,
                PSTR Buffer,
                SIZE_T BufferSize)
{
    SIZE_T Index;
    if (Function != NULL && Function->Symbol != NULL &&
        Function->Symbol->Name != NULL && Function->Symbol->Name[0] != 0)
    {
        _snprintf(Buffer, BufferSize, "%s!%s",
                  Function->Symbol->ModuleName != NULL ?
                    Function->Symbol->ModuleName : "<module>",
                  Function->Symbol->Name);
    }
    else if (Function != NULL)
    {
        _snprintf(Buffer, BufferSize, "module-%I64x!0x%I64x",
                  Function->Key.ModuleId, Function->Key.Address);
    }
    else
    {
        _snprintf(Buffer, BufferSize, "<unknown>");
    }
    Buffer[BufferSize - 1] = ANSI_NULL;
    for (Index = 0; Buffer[Index] != ANSI_NULL; ++Index)
    {
        if (Buffer[Index] == ';' || Buffer[Index] == ',' ||
            Buffer[Index] == '\r' || Buffer[Index] == '\n' ||
            Buffer[Index] == '"')
            Buffer[Index] = '_';
    }
}

static BOOL
RperfExportMetadata(FILE *File,
                    const RPERF_ANALYSIS *Analysis,
                    PCSTR Format)
{
    return fprintf(File, "# rperf-export=%s\n", Format) >= 0 &&
           fprintf(File, "# metric=%u\n", Analysis->Metric) >= 0 &&
           fprintf(File, "# samples=%I64u\n", Analysis->TotalSamples) >= 0 &&
           fprintf(File, "# weight=%I64u\n", Analysis->TotalWeight) >= 0 &&
           fprintf(File, "# complete=%u\n",
                   Analysis->Recording->Info.Complete) >= 0 &&
           fprintf(File, "# backend=%u\n",
                   Analysis->Recording->Info.Backend) >= 0 &&
           fprintf(File, "# lost-records=%I64u\n",
                   Analysis->MatchedLostRecords) >= 0 &&
           fprintf(File, "# filter-mask=0x%08lx\n",
                   Analysis->Filter.Enabled) >= 0;
}

static BOOL
RperfExportFoldedNode(FILE *File,
                      const RPERF_ANALYSIS *Analysis,
                      SIZE_T NodeIndex,
                      SIZE_T *Path,
                      SIZE_T Depth,
                      HANDLE CancelEvent)
{
    const RPERF_CALL_NODE *Node = &Analysis->TopDown[NodeIndex];
    SIZE_T Child;

    if (Depth >= RPERF_MODEL_MAX_FRAMES ||
        (CancelEvent != NULL &&
         WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0))
    {
        SetLastError(Depth >= RPERF_MODEL_MAX_FRAMES ?
                     ERROR_BUFFER_OVERFLOW : ERROR_CANCELLED);
        return FALSE;
    }
    Path[Depth++] = NodeIndex;
    Child = Node->FirstChild;
    if (Child == (SIZE_T)-1)
    {
        SIZE_T Index;
        for (Index = 0; Index < Depth; ++Index)
        {
            const RPERF_CALL_NODE *Part = &Analysis->TopDown[Path[Index]];
            const RPERF_FUNCTION_COST *Function =
                RperfAnalysisFindFunction(Analysis, &Part->Key);
            CHAR Name[1024];
            RperfExportName(Function, Name, sizeof(Name));
            if ((Index != 0 && fputc(';', File) == EOF) ||
                fputs(Name, File) == EOF)
                return FALSE;
        }
        return fprintf(File, " %I64u\n", Node->Weight) >= 0;
    }
    while (Child != (SIZE_T)-1)
    {
        SIZE_T Next;
        if (Child >= Analysis->TopDownCount)
            return FALSE;
        Next = Analysis->TopDown[Child].NextSibling;
        if (!RperfExportFoldedNode(File, Analysis, Child,
                                   Path, Depth, CancelEvent))
            return FALSE;
        Child = Next;
    }
    return TRUE;
}

static BOOL
RperfWriteFolded(FILE *File,
                 const RPERF_ANALYSIS *Analysis,
                 HANDLE CancelEvent)
{
    SIZE_T Path[RPERF_MODEL_MAX_FRAMES];
    SIZE_T Child;
    if (!RperfExportMetadata(File, Analysis, "folded-v1") ||
        Analysis->TopDownCount == 0)
        return FALSE;
    Child = Analysis->TopDown[0].FirstChild;
    while (Child != (SIZE_T)-1)
    {
        SIZE_T Next = Analysis->TopDown[Child].NextSibling;
        if (!RperfExportFoldedNode(File, Analysis, Child,
                                   Path, 0, CancelEvent))
            return FALSE;
        Child = Next;
    }
    return TRUE;
}

static int __cdecl
RperfFunctionOrder(const void *Left,
                   const void *Right)
{
    const RPERF_FUNCTION_COST *A = Left;
    const RPERF_FUNCTION_COST *B = Right;
    if (A->Key.ModuleId < B->Key.ModuleId) return -1;
    if (A->Key.ModuleId > B->Key.ModuleId) return 1;
    if (A->Key.Address < B->Key.Address) return -1;
    if (A->Key.Address > B->Key.Address) return 1;
    return 0;
}

static BOOL
RperfWriteCsv(FILE *File,
              const RPERF_ANALYSIS *Analysis,
              HANDLE CancelEvent)
{
    RPERF_FUNCTION_COST *Ordered;
    SIZE_T Index;
    if (!RperfExportMetadata(File, Analysis, "csv-v1") ||
        fputs("module_id,address,name,self_weight,inclusive_weight,"
              "self_samples,inclusive_samples,total_weight,total_samples\n",
              File) == EOF)
        return FALSE;
    if (Analysis->FunctionCount > ((SIZE_T)-1) / sizeof(*Ordered))
        return FALSE;
    Ordered = HeapAlloc(GetProcessHeap(), 0,
                        Analysis->FunctionCount * sizeof(*Ordered));
    if (Ordered == NULL && Analysis->FunctionCount != 0)
        return FALSE;
    CopyMemory(Ordered, Analysis->Functions,
               Analysis->FunctionCount * sizeof(*Ordered));
    qsort(Ordered, Analysis->FunctionCount,
          sizeof(*Ordered), RperfFunctionOrder);
    for (Index = 0; Index < Analysis->FunctionCount; ++Index)
    {
        CHAR Name[1024];
        if ((Index & 1023) == 0 && CancelEvent != NULL &&
            WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
        {
            HeapFree(GetProcessHeap(), 0, Ordered);
            SetLastError(ERROR_CANCELLED);
            return FALSE;
        }
        RperfExportName(&Ordered[Index], Name, sizeof(Name));
        if (fprintf(File,
                    "%I64x,%I64x,\"%s\",%I64u,%I64u,%I64u,%I64u,%I64u,%I64u\n",
                    Ordered[Index].Key.ModuleId,
                    Ordered[Index].Key.Address,
                    Name,
                    Ordered[Index].SelfWeight,
                    Ordered[Index].InclusiveWeight,
                    Ordered[Index].SelfSamples,
                    Ordered[Index].InclusiveSamples,
                    Analysis->TotalWeight,
                    Analysis->TotalSamples) < 0)
        {
            HeapFree(GetProcessHeap(), 0, Ordered);
            return FALSE;
        }
    }
    HeapFree(GetProcessHeap(), 0, Ordered);
    return TRUE;
}

BOOL
RperfExportAnalysis(PCWSTR Path,
                    RPERF_EXPORT_KIND Kind,
                    const RPERF_ANALYSIS *Analysis,
                    HANDLE CancelEvent)
{
    FILE *File;
    BOOL Result;
    if (Path == NULL || Analysis == NULL)
        return FALSE;
    if (Kind == RperfExportRawV2)
        return RperfCodecSave(Path, RperfCodecV2Binary,
                              Analysis->Recording, CancelEvent,
                              NULL, NULL);
    File = _wfopen(Path, L"wb");
    if (File == NULL)
        return FALSE;
    if (Kind == RperfExportFolded)
        Result = RperfWriteFolded(File, Analysis, CancelEvent);
    else if (Kind == RperfExportCsv)
        Result = RperfWriteCsv(File, Analysis, CancelEvent);
    else
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        Result = FALSE;
    }
    if (fclose(File) != 0)
        Result = FALSE;
    return Result;
}

static LONGLONG
RperfNormalizedDelta(ULONGLONG BaselineValue,
                     ULONGLONG BaselineTotal,
                     ULONGLONG CandidateValue,
                     ULONGLONG CandidateTotal)
{
    long double Baseline, Candidate, Delta;
    if (BaselineTotal == 0 || CandidateTotal == 0)
        return 0;
    Baseline = (long double)BaselineValue / BaselineTotal;
    Candidate = (long double)CandidateValue / CandidateTotal;
    Delta = (Candidate - Baseline) * 1000000.0L;
    if (Delta > 9223372036854775807.0L)
        return 0x7fffffffffffffffLL;
    if (Delta < -9223372036854775807.0L)
        return -0x7fffffffffffffffLL;
    return (LONGLONG)Delta;
}

static ULONGLONG
RperfIdentityHash(const RPERF_MODULE *Module)
{
    ULONGLONG Hash = 1469598103934665603ULL;
    SIZE_T Index;
    const UCHAR *Bytes;
    ULONG Values[4];

    Values[0] = Module->Architecture;
    Values[1] = Module->TimeDateStamp;
    Values[2] = Module->Checksum;
    Values[3] = Module->DebugAge;
    Bytes = (const UCHAR *)Values;
    for (Index = 0; Index < sizeof(Values); ++Index)
        Hash = (Hash ^ Bytes[Index]) * 1099511628211ULL;
    for (Index = 0; Index < sizeof(Module->DebugId); ++Index)
        Hash = (Hash ^ Module->DebugId[Index]) * 1099511628211ULL;
    if (Module->Path != NULL)
    {
        for (Index = 0; Module->Path[Index] != UNICODE_NULL; ++Index)
        {
            WCHAR Character = Module->Path[Index];
            if (Character >= L'A' && Character <= L'Z')
                Character += L'a' - L'A';
            Hash = (Hash ^ Character) * 1099511628211ULL;
        }
    }
    return Hash;
}

static VOID
RperfMakeComparisonKeys(const RPERF_ANALYSIS *Analysis,
                        RPERF_FUNCTION_COST *Functions,
                        SIZE_T Count)
{
    SIZE_T Index;
    for (Index = 0; Index < Count; ++Index)
    {
        const RPERF_MODULE *Module =
            RperfRecordingFindModule(Analysis->Recording,
                                     Functions[Index].Key.ModuleId);
        if (Module != NULL && Functions[Index].Key.Address >= Module->Base)
        {
            Functions[Index].Key.ModuleId = RperfIdentityHash(Module);
            Functions[Index].Key.Address -= Module->Base;
        }
    }
}

RPERF_COMPARISON *
RperfCompareAnalyses(RPERF_ANALYSIS *Baseline,
                     RPERF_ANALYSIS *Candidate,
                     HANDLE CancelEvent)
{
    RPERF_COMPARISON *Result;
    RPERF_FUNCTION_COST *A = NULL, *B = NULL;
    SIZE_T Ai = 0, Bi = 0, Count = 0, Maximum;

    if (Baseline == NULL || Candidate == NULL ||
        Baseline->Metric != Candidate->Metric ||
        Baseline->FunctionCount > (SIZE_T)-1 - Candidate->FunctionCount)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }
    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Result));
    if (Result == NULL)
        return NULL;
    Maximum = Baseline->FunctionCount + Candidate->FunctionCount;
    Result->Entries = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                Maximum * sizeof(*Result->Entries));
    A = HeapAlloc(GetProcessHeap(), 0,
                  Baseline->FunctionCount * sizeof(*A));
    B = HeapAlloc(GetProcessHeap(), 0,
                  Candidate->FunctionCount * sizeof(*B));
    if ((Maximum != 0 && Result->Entries == NULL) ||
        (Baseline->FunctionCount != 0 && A == NULL) ||
        (Candidate->FunctionCount != 0 && B == NULL))
        goto Failure;
    CopyMemory(A, Baseline->Functions,
               Baseline->FunctionCount * sizeof(*A));
    CopyMemory(B, Candidate->Functions,
               Candidate->FunctionCount * sizeof(*B));
    RperfMakeComparisonKeys(Baseline, A, Baseline->FunctionCount);
    RperfMakeComparisonKeys(Candidate, B, Candidate->FunctionCount);
    qsort(A, Baseline->FunctionCount, sizeof(*A), RperfFunctionOrder);
    qsort(B, Candidate->FunctionCount, sizeof(*B), RperfFunctionOrder);
    while (Ai < Baseline->FunctionCount || Bi < Candidate->FunctionCount)
    {
        RPERF_COMPARISON_ENTRY *Entry = &Result->Entries[Count++];
        int Order;
        if ((Count & 1023) == 0 && CancelEvent != NULL &&
            WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
        {
            SetLastError(ERROR_CANCELLED);
            goto Failure;
        }
        if (Ai == Baseline->FunctionCount)
            Order = 1;
        else if (Bi == Candidate->FunctionCount)
            Order = -1;
        else
            Order = RperfFunctionOrder(&A[Ai], &B[Bi]);
        if (Order <= 0)
        {
            Entry->Key = A[Ai].Key;
            Entry->BaselineSelf = A[Ai].SelfWeight;
            Entry->BaselineInclusive = A[Ai].InclusiveWeight;
            Ai++;
        }
        if (Order >= 0)
        {
            Entry->Key = B[Bi].Key;
            Entry->CandidateSelf = B[Bi].SelfWeight;
            Entry->CandidateInclusive = B[Bi].InclusiveWeight;
            Bi++;
        }
        Entry->SelfDeltaPpm =
            RperfNormalizedDelta(Entry->BaselineSelf,
                                 Baseline->TotalWeight,
                                 Entry->CandidateSelf,
                                 Candidate->TotalWeight);
        Entry->InclusiveDeltaPpm =
            RperfNormalizedDelta(Entry->BaselineInclusive,
                                 Baseline->TotalWeight,
                                 Entry->CandidateInclusive,
                                 Candidate->TotalWeight);
    }
    Result->Baseline = Baseline;
    Result->Candidate = Candidate;
    Result->EntryCount = Count;
    RperfAnalysisAddRef(Baseline);
    RperfAnalysisAddRef(Candidate);
    HeapFree(GetProcessHeap(), 0, A);
    HeapFree(GetProcessHeap(), 0, B);
    return Result;

Failure:
    if (A != NULL) HeapFree(GetProcessHeap(), 0, A);
    if (B != NULL) HeapFree(GetProcessHeap(), 0, B);
    RperfComparisonDestroy(Result);
    return NULL;
}

VOID
RperfComparisonDestroy(RPERF_COMPARISON *Comparison)
{
    if (Comparison == NULL)
        return;
    if (Comparison->Baseline != NULL)
        RperfAnalysisRelease(Comparison->Baseline);
    if (Comparison->Candidate != NULL)
        RperfAnalysisRelease(Comparison->Candidate);
    if (Comparison->Entries != NULL)
        HeapFree(GetProcessHeap(), 0, Comparison->Entries);
    HeapFree(GetProcessHeap(), 0, Comparison);
}
