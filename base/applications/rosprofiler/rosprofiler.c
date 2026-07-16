/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Process recorder and profile analysis user interface
 */

#include "rosprofiler.h"
#include "profiler_recorder.h"
#include "profiler_codec.h"
#include "profiler_controller.h"
#include "profiler_legacy_bridge.h"
#include "profiler_symbolizer_dbghelp.h"
#include "resource.h"

#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define RPERF_MAIN_CLASS L"RosProfilerMainWindow"
#define WM_RPERF_JOB_PROGRESS (WM_APP + 11)
#define WM_RPERF_JOB_COMPLETE (WM_APP + 12)
#define RPERF_OFFLINE_PATH_CHARS (MAX_PATH * 8)

#define IDC_PROCESS 1001
#define IDC_REFRESH 1002
#define IDC_INTERVAL 1003
#define IDC_DURATION 1004
#define IDC_RECORD 1005
#define IDC_STOP 1006
#define IDC_OPEN 1007
#define IDC_RESET_ZOOM 1008
#define IDC_MODE 1009
#define IDC_TAB 1010
#define IDC_FLAME 1011
#define IDC_FUNCTIONS 1012
#define IDC_STATUS 1013
#define IDC_LAUNCH 1014
#define IDC_SEARCH 1015
#define IDC_THREAD_FILTER 1016
#define IDC_TIME_START 1017
#define IDC_TIME_END 1018
#define IDC_APPLY_FILTER 1019
#define IDC_RESET_FILTER 1020
#define IDC_TIMELINE 1021
#define IDC_SESSION_SUMMARY 1022
#define IDC_BACKEND 1023
#define IDC_CPU_ONLY 1024

#define IDM_FILE_OPEN 2001
#define IDM_FILE_EXIT 2002
#define IDM_CAPTURE_REFRESH 2010
#define IDM_CAPTURE_RECORD 2011
#define IDM_CAPTURE_STOP 2012
#define IDM_CAPTURE_LAUNCH 2013
#define IDM_VIEW_RESET_ZOOM 2020
#define IDM_HELP_ABOUT 2030

typedef struct _RPERF_SUMMARY_ROW
{
    const RPERF_SYMBOL *Symbol;
    SIZE_T SymbolIndex;
} RPERF_SUMMARY_ROW;

typedef struct _RPERF_APP
{
    HINSTANCE Instance;
    HWND MainWindow;
    HWND ProcessLabel;
    HWND ProcessCombo;
    HWND RefreshButton;
    HWND IntervalEdit;
    HWND DurationEdit;
    HWND RecordButton;
    HWND LaunchButton;
    HWND StopButton;
    HWND OpenButton;
    HWND ResetZoomButton;
    HWND ModeLabel;
    HWND IntervalLabel;
    HWND DurationLabel;
    HWND SearchLabel;
    HWND SearchEdit;
    HWND ThreadLabel;
    HWND ThreadCombo;
    HWND TimeLabel;
    HWND TimeStartEdit;
    HWND TimeEndLabel;
    HWND TimeEndEdit;
    HWND ApplyFilterButton;
    HWND ResetFilterButton;
    HWND CpuOnlyCheck;
    HWND Tab;
    HWND FlameGraph;
    HWND FunctionList;
    HWND Timeline;
    HWND SessionSummary;
    HWND StatusBar;
    WCHAR Title[128];
    RPERF_PROCESS_INFO *Processes;
    SIZE_T ProcessCount;
    SIZE_T ProcessCapacity;
    INT SortColumn;
    BOOL SortAscending;
    WCHAR LaunchPath[MAX_PATH];
    WCHAR LaunchArguments[2048];
    WCHAR LaunchDirectory[MAX_PATH];
    RPERF_SESSION Session;
    HWND BackendCombo;
    RPERF_RECORDER *Recorder;
    HANDLE RecorderMonitor;
    RPERF_CODEC_STREAM *RecorderStream;
    WCHAR RecorderLogPath[MAX_PATH];
    BOOL HasSymbolSummary;
    RPERF_SYMBOLIZATION_SUMMARY SymbolSummary;
    RPERF_SESSION_CONTROLLER Controller;
    BOOL ControllerInitialized;
    BOOL Processing;
    ULONGLONG JobGeneration;
    RPERF_JOB_KIND JobKind;
    WCHAR JobPath[MAX_PATH];
    RPERF_SYMBOL_PROVIDER *JobSymbolProvider;
    DWORD JobSymbolError;
    BOOL JobHasSymbolSummary;
    RPERF_SYMBOLIZATION_SUMMARY JobSymbolSummary;
} RPERF_APP;

static RPERF_APP App;

static VOID
RperfSetStatus(PCWSTR Text)
{
    SendMessageW(App.StatusBar, SB_SETTEXTW, 0, (LPARAM)Text);
}

static VOID
RperfShowSystemError(HWND Owner,
                     PCWSTR Operation,
                     DWORD Error)
{
    PWSTR SystemMessage = NULL;
    WCHAR Message[768];

    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                   FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL,
                   Error,
                   0,
                   (PWSTR)&SystemMessage,
                   0,
                   NULL);
    if (SystemMessage != NULL)
    {
        _snwprintf(Message,
                   ARRAYSIZE(Message),
                   L"%s failed (error %lu):\n\n%s",
                   Operation,
                   Error,
                   SystemMessage);
        LocalFree(SystemMessage);
    }
    else
    {
        _snwprintf(Message,
                   ARRAYSIZE(Message),
                   L"%s failed with error %lu.",
                   Operation,
                   Error);
    }
    Message[ARRAYSIZE(Message) - 1] = UNICODE_NULL;
    MessageBoxW(Owner, Message, App.Title, MB_OK | MB_ICONERROR);
}

static int __cdecl
RperfCompareProcesses(const void *Left,
                      const void *Right)
{
    const RPERF_PROCESS_INFO *LeftItem = Left;
    const RPERF_PROCESS_INFO *RightItem = Right;
    int Result = lstrcmpiW(LeftItem->Name, RightItem->Name);

    if (Result != 0)
        return Result;
    if (LeftItem->ProcessId < RightItem->ProcessId)
        return -1;
    if (LeftItem->ProcessId > RightItem->ProcessId)
        return 1;
    return 0;
}

static DWORD
RperfGetSelectedProcessId(VOID)
{
    LRESULT Selection = SendMessageW(App.ProcessCombo, CB_GETCURSEL, 0, 0);
    LRESULT ProcessId;

    if (Selection == CB_ERR)
        return 0;
    ProcessId = SendMessageW(App.ProcessCombo,
                             CB_GETITEMDATA,
                             Selection,
                             0);
    if (ProcessId == CB_ERR)
        return 0;
    return (DWORD)ProcessId;
}

static const RPERF_PROCESS_INFO *
RperfGetSelectedProcess(VOID)
{
    DWORD ProcessId = RperfGetSelectedProcessId();
    SIZE_T Index;

    for (Index = 0; Index < App.ProcessCount; ++Index)
    {
        if (App.Processes[Index].ProcessId == ProcessId)
            return &App.Processes[Index];
    }
    return NULL;
}

static VOID
RperfRefreshProcesses(VOID)
{
    DWORD PreviousProcessId = RperfGetSelectedProcessId();
    RPERF_PROCESS_INFO *Processes = NULL;
    SIZE_T ProcessCount = 0;
    SIZE_T Index;
    LRESULT Selection = CB_ERR;
    WCHAR DisplayName[MAX_PATH + 40];

    SendMessageW(App.ProcessCombo, CB_RESETCONTENT, 0, 0);
    if (!RperfEnumerateProcesses(&Processes, &ProcessCount))
    {
        RperfSetStatus(L"Could not enumerate processes.");
        return;
    }
    RperfFreeProcesses(App.Processes);
    App.Processes = Processes;
    App.ProcessCount = ProcessCount;
    App.ProcessCapacity = ProcessCount;

    qsort(App.Processes,
          App.ProcessCount,
          sizeof(*App.Processes),
          RperfCompareProcesses);

    for (Index = 0; Index < App.ProcessCount; ++Index)
    {
        LRESULT ComboIndex;
        _snwprintf(DisplayName,
                   ARRAYSIZE(DisplayName),
                   L"%s (PID %lu)",
                   App.Processes[Index].Name,
                   App.Processes[Index].ProcessId);
        DisplayName[ARRAYSIZE(DisplayName) - 1] = UNICODE_NULL;
        ComboIndex = SendMessageW(App.ProcessCombo,
                                  CB_ADDSTRING,
                                  0,
                                  (LPARAM)DisplayName);
        if (ComboIndex != CB_ERR && ComboIndex != CB_ERRSPACE)
        {
            SendMessageW(App.ProcessCombo,
                         CB_SETITEMDATA,
                         ComboIndex,
                         (LPARAM)App.Processes[Index].ProcessId);
            if (App.Processes[Index].ProcessId == PreviousProcessId)
                Selection = ComboIndex;
        }
    }

    if (Selection == CB_ERR && App.ProcessCount != 0)
        Selection = 0;
    if (Selection != CB_ERR)
        SendMessageW(App.ProcessCombo, CB_SETCURSEL, Selection, 0);

    _snwprintf(DisplayName,
               ARRAYSIZE(DisplayName),
               L"Ready. %Iu processes available.",
               App.ProcessCount);
    DisplayName[ARRAYSIZE(DisplayName) - 1] = UNICODE_NULL;
    RperfSetStatus(DisplayName);
}

static int __cdecl
RperfCompareSummaryRows(const void *Left,
                        const void *Right)
{
    const RPERF_SUMMARY_ROW *LeftRow = Left;
    const RPERF_SUMMARY_ROW *RightRow = Right;
    int Result = 0;

    switch (App.SortColumn)
    {
        case 0:
            Result = lstrcmpiA(LeftRow->Symbol->Name,
                               RightRow->Symbol->Name);
            break;
        case 1:
            Result = lstrcmpiA(LeftRow->Symbol->Module,
                               RightRow->Symbol->Module);
            break;
        case 2:
            if (LeftRow->Symbol->Exclusive < RightRow->Symbol->Exclusive)
                Result = -1;
            else if (LeftRow->Symbol->Exclusive >
                     RightRow->Symbol->Exclusive)
                Result = 1;
            break;
        case 3:
        case 4:
        default:
            if (LeftRow->Symbol->Inclusive < RightRow->Symbol->Inclusive)
                Result = -1;
            else if (LeftRow->Symbol->Inclusive >
                     RightRow->Symbol->Inclusive)
                Result = 1;
            break;
    }

    if (!App.SortAscending)
        Result = -Result;
    if (Result == 0)
        Result = lstrcmpiA(LeftRow->Symbol->Name,
                           RightRow->Symbol->Name);
    if (Result == 0)
    {
        if (LeftRow->Symbol->FunctionAddress <
            RightRow->Symbol->FunctionAddress)
        {
            Result = -1;
        }
        else if (LeftRow->Symbol->FunctionAddress >
                 RightRow->Symbol->FunctionAddress)
        {
            Result = 1;
        }
    }
    return Result;
}

static BOOL
RperfWideContainsText(PCWSTR Text,
                      PCWSTR Search)
{
    SIZE_T TextLength, SearchLength, Index;

    if (Search == NULL || *Search == UNICODE_NULL)
        return TRUE;
    TextLength = wcslen(Text);
    SearchLength = wcslen(Search);
    if (SearchLength > TextLength)
        return FALSE;

    for (Index = 0; Index + SearchLength <= TextLength; ++Index)
    {
        if (CompareStringW(LOCALE_USER_DEFAULT,
                           NORM_IGNORECASE,
                           Text + Index,
                           (INT)SearchLength,
                           Search,
                           (INT)SearchLength) == CSTR_EQUAL)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
RperfAnsiToWide(PCSTR Source,
                PWSTR Destination,
                SIZE_T DestinationCount)
{
    if (DestinationCount == 0)
        return;
    if (!MultiByteToWideChar(CP_ACP,
                             0,
                             Source,
                             -1,
                             Destination,
                             (INT)DestinationCount))
    {
        lstrcpynW(Destination, L"<unknown>", (INT)DestinationCount);
    }
}

static VOID
RperfSetListText(INT Item,
                 INT SubItem,
                 PWSTR Text)
{
    LVITEMW ListItem;

    ZeroMemory(&ListItem, sizeof(ListItem));
    ListItem.iSubItem = SubItem;
    ListItem.pszText = Text;
    SendMessageW(App.FunctionList,
                 LVM_SETITEMTEXTW,
                 Item,
                 (LPARAM)&ListItem);
}

static VOID
RperfPopulateFunctions(VOID)
{
    RPERF_SUMMARY_ROW *Rows;
    SIZE_T RowCount = 0;
    SIZE_T Index;
    WCHAR Search[128];

    SendMessageW(App.FunctionList, LVM_DELETEALLITEMS, 0, 0);
    if (App.Session.SymbolCount == 0 ||
        App.Session.FilteredSampleCount == 0)
        return;

    GetWindowTextW(App.SearchEdit, Search, ARRAYSIZE(Search));

    Rows = HeapAlloc(GetProcessHeap(),
                     0,
                     App.Session.SymbolCount * sizeof(*Rows));
    if (Rows == NULL)
        return;

    for (Index = 0; Index < App.Session.SymbolCount; ++Index)
    {
        if (App.Session.Symbols[Index].Inclusive != 0)
        {
            WCHAR Formatted[384];

            RperfFormatSymbol(&App.Session,
                              App.Session.Symbols[Index].FunctionAddress,
                              Formatted,
                              ARRAYSIZE(Formatted));
            if (RperfWideContainsText(Formatted, Search))
            {
                Rows[RowCount].Symbol = &App.Session.Symbols[Index];
                Rows[RowCount].SymbolIndex = Index;
                RowCount++;
            }
        }
    }

    qsort(Rows, RowCount, sizeof(*Rows), RperfCompareSummaryRows);
    SendMessageW(App.FunctionList, WM_SETREDRAW, FALSE, 0);
    for (Index = 0; Index < RowCount; ++Index)
    {
        const RPERF_SYMBOL *Symbol = Rows[Index].Symbol;
        LVITEMW Item;
        WCHAR Function[384], Module[96], Number[64], Percentage[64];
        INT ListIndex;

        if (strcmp(Symbol->Name, "<unknown>") == 0)
        {
            _snwprintf(Function,
                       ARRAYSIZE(Function),
                       L"0x%I64x",
                       Symbol->FunctionAddress);
        }
        else
        {
            RperfAnsiToWide(Symbol->Name,
                            Function,
                            ARRAYSIZE(Function));
        }
        Function[ARRAYSIZE(Function) - 1] = UNICODE_NULL;
        RperfAnsiToWide(Symbol->Module, Module, ARRAYSIZE(Module));

        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT | LVIF_PARAM;
        Item.iItem = (INT)Index;
        Item.pszText = Function;
        Item.lParam = (LPARAM)Rows[Index].SymbolIndex;
        ListIndex = (INT)SendMessageW(App.FunctionList,
                                      LVM_INSERTITEMW,
                                      0,
                                      (LPARAM)&Item);
        if (ListIndex < 0)
            continue;

        RperfSetListText(ListIndex, 1, Module);
        _snwprintf(Number,
                   ARRAYSIZE(Number),
                   L"%I64u",
                   Symbol->Exclusive);
        RperfSetListText(ListIndex, 2, Number);
        _snwprintf(Number,
                   ARRAYSIZE(Number),
                   L"%I64u",
                   Symbol->Inclusive);
        RperfSetListText(ListIndex, 3, Number);
        _snwprintf(Percentage,
                   ARRAYSIZE(Percentage),
                   L"%.2f%%",
                   100.0 * (double)Symbol->Inclusive /
                   (double)App.Session.FilteredSampleCount);
        RperfSetListText(ListIndex, 4, Percentage);
    }
    SendMessageW(App.FunctionList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(App.FunctionList, NULL, TRUE);
    HeapFree(GetProcessHeap(), 0, Rows);
}

static VOID
RperfSetSessionStatus(VOID)
{
    WCHAR Status[512];

    if (App.Session.SampleCount == 0)
    {
        RperfSetStatus(L"Ready.");
        return;
    }

    if (App.HasSymbolSummary)
    {
        _snwprintf(Status,
                   ARRAYSIZE(Status),
                   L"%s (PID %lu): %Iu/%Iu samples, %.3f s; capture: "
                   L"%I64u/%I64u accepted, %I64u failed; symbols: "
                   L"PDB %I64u, rsym %I64u, other %I64u, fallback %I64u, "
                   L"mismatch %I64u%s.",
                   App.Session.ProcessName,
                   App.Session.ProcessId,
                   App.Session.FilteredSampleCount,
                   App.Session.SampleCount,
                   (double)App.Session.ElapsedUs / 1000000.0,
                   App.Session.Counters.SuccessfulSamples,
                   App.Session.Counters.AttemptedSamples,
                   App.Session.Counters.FailedSamples,
                   App.SymbolSummary.Pdb,
                   App.SymbolSummary.RosSym,
                   App.SymbolSummary.Dwarf + App.SymbolSummary.Coff +
                       App.SymbolSummary.Export,
                   App.SymbolSummary.ModuleOffset,
                   App.SymbolSummary.IdentityMismatch,
                   App.Session.LogComplete ? L"" :
                       L", recovered incomplete log");
    }
    else
    {
        _snwprintf(Status,
                   ARRAYSIZE(Status),
                   L"%s (PID %lu): %Iu of %Iu samples in view, "
                   L"%I64u/%I64u accepted, %I64u failed, "
                   L"%I64u missed cadence ticks, %.3f s elapsed%s.",
                   App.Session.ProcessName,
                   App.Session.ProcessId,
                   App.Session.FilteredSampleCount,
                   App.Session.SampleCount,
                   App.Session.Counters.SuccessfulSamples,
                   App.Session.Counters.AttemptedSamples,
                   App.Session.Counters.FailedSamples,
                   App.Session.Counters.MissedCadenceTicks,
                   (double)App.Session.ElapsedUs / 1000000.0,
                   App.Session.LogComplete ? L"" :
                       L", recovered incomplete log");
    }
    Status[ARRAYSIZE(Status) - 1] = UNICODE_NULL;
    RperfSetStatus(Status);
}

static int __cdecl
RperfCompareThreadIds(const void *Left,
                      const void *Right)
{
    DWORD LeftId = *(const DWORD *)Left;
    DWORD RightId = *(const DWORD *)Right;

    if (LeftId < RightId)
        return -1;
    if (LeftId > RightId)
        return 1;
    return 0;
}

static VOID
RperfPopulateThreadFilter(VOID)
{
    DWORD *ThreadIds = NULL;
    SIZE_T Index;
    LRESULT Item;

    SendMessageW(App.ThreadCombo, CB_RESETCONTENT, 0, 0);
    Item = SendMessageW(App.ThreadCombo,
                        CB_ADDSTRING,
                        0,
                        (LPARAM)L"All threads");
    if (Item != CB_ERR && Item != CB_ERRSPACE)
        SendMessageW(App.ThreadCombo, CB_SETITEMDATA, Item, 0);

    if (App.Session.SampleCount != 0 &&
        App.Session.SampleCount <= ((SIZE_T)-1) / sizeof(*ThreadIds))
    {
        ThreadIds = HeapAlloc(GetProcessHeap(),
                              0,
                              App.Session.SampleCount * sizeof(*ThreadIds));
    }
    if (ThreadIds != NULL)
    {
        for (Index = 0; Index < App.Session.SampleCount; ++Index)
            ThreadIds[Index] = App.Session.Samples[Index].ThreadId;
        qsort(ThreadIds,
              App.Session.SampleCount,
              sizeof(*ThreadIds),
              RperfCompareThreadIds);

        for (Index = 0; Index < App.Session.SampleCount; ++Index)
        {
            WCHAR Label[64];

            if (Index != 0 && ThreadIds[Index] == ThreadIds[Index - 1])
                continue;
            _snwprintf(Label,
                       ARRAYSIZE(Label),
                       L"TID %lu",
                       ThreadIds[Index]);
            Label[ARRAYSIZE(Label) - 1] = UNICODE_NULL;
            Item = SendMessageW(App.ThreadCombo,
                                CB_ADDSTRING,
                                0,
                                (LPARAM)Label);
            if (Item != CB_ERR && Item != CB_ERRSPACE)
            {
                SendMessageW(App.ThreadCombo,
                             CB_SETITEMDATA,
                             Item,
                             (LPARAM)ThreadIds[Index]);
            }
        }
        HeapFree(GetProcessHeap(), 0, ThreadIds);
    }
    SendMessageW(App.ThreadCombo, CB_SETCURSEL, 0, 0);
}

static VOID
RperfFormatSeconds(ULONGLONG TimeUs,
                   PWSTR Buffer,
                   SIZE_T BufferCount)
{
    _snwprintf(Buffer,
               BufferCount,
               L"%I64u.%06I64u",
               TimeUs / 1000000,
               TimeUs % 1000000);
    Buffer[BufferCount - 1] = UNICODE_NULL;
}

static VOID
RperfSetTimeControls(ULONGLONG StartUs,
                     ULONGLONG EndUs)
{
    WCHAR Text[64];

    RperfFormatSeconds(StartUs, Text, ARRAYSIZE(Text));
    SetWindowTextW(App.TimeStartEdit, Text);
    RperfFormatSeconds(EndUs, Text, ARRAYSIZE(Text));
    SetWindowTextW(App.TimeEndEdit, Text);
}

static BOOL
RperfParseSeconds(HWND Edit,
                  ULONGLONG *TimeUs)
{
    WCHAR Text[64];
    PCWSTR Cursor;
    ULONGLONG Seconds = 0, Fraction = 0;
    ULONG FractionDigits = 0;
    const ULONGLONG Maximum = (ULONGLONG)-1;

    GetWindowTextW(Edit, Text, ARRAYSIZE(Text));
    Cursor = Text;
    while (*Cursor == L' ' || *Cursor == L'\t')
        ++Cursor;
    if (*Cursor < L'0' || *Cursor > L'9')
        return FALSE;

    while (*Cursor >= L'0' && *Cursor <= L'9')
    {
        ULONG Digit = *Cursor++ - L'0';
        if (Seconds > (Maximum - Digit) / 10)
            return FALSE;
        Seconds = Seconds * 10 + Digit;
    }
    if (*Cursor == L'.')
    {
        ++Cursor;
        if (*Cursor < L'0' || *Cursor > L'9')
            return FALSE;
        while (*Cursor >= L'0' && *Cursor <= L'9')
        {
            if (FractionDigits == 6)
                return FALSE;
            Fraction = Fraction * 10 + (*Cursor++ - L'0');
            FractionDigits++;
        }
    }
    while (*Cursor == L' ' || *Cursor == L'\t')
        ++Cursor;
    if (*Cursor != UNICODE_NULL)
        return FALSE;
    while (FractionDigits++ < 6)
        Fraction *= 10;
    if (Seconds > (Maximum - Fraction) / 1000000)
        return FALSE;

    *TimeUs = Seconds * 1000000 + Fraction;
    return TRUE;
}

static PCWSTR
RperfCompletionText(RPERF_COMPLETION_REASON Reason)
{
    switch (Reason)
    {
        case RperfCompletionDuration:
            return L"requested duration reached";
        case RperfCompletionUserStop:
            return L"stopped by user";
        case RperfCompletionTargetExit:
            return L"target exited";
        case RperfCompletionError:
            return L"recorder error";
        case RperfCompletionIncomplete:
        default:
            return L"incomplete log recovered";
    }
}

static PCWSTR
RperfBackendSummaryName(RPERF_BACKEND_KIND Backend)
{
    switch (Backend)
    {
        case RperfBackendKernel:
            return L"kernel on-CPU timer samples (RosProf device)";
        case RperfBackendEtw:
            return L"ETW sampled-profile events (documented APIs)";
        case RperfBackendFake:
            return L"synthetic contract-test recorder";
        case RperfBackendIntrusive:
        default:
            return L"intrusive user-mode all-thread wall-clock snapshots";
    }
}

static PCWSTR
RperfBackendSummaryCaveat(RPERF_BACKEND_KIND Backend)
{
    switch (Backend)
    {
        case RperfBackendKernel:
            return L"This recording sampled from the kernel profile timer "
                   L"without suspending target threads; kernel-mode samples "
                   L"carry bounded interrupt-time call chains where the "
                   L"kernel supports them, user-mode samples remain the "
                   L"interrupted instruction, and PMU events are not "
                   L"captured yet.";
        case RperfBackendEtw:
            return L"This recording used the documented ETW sampled-profile "
                   L"backend; capability gaps are reported at capture time "
                   L"rather than silently substituted.";
        case RperfBackendFake:
            return L"This recording came from the deterministic synthetic "
                   L"recorder used for contract tests; it does not describe "
                   L"a real workload.";
        case RperfBackendIntrusive:
        default:
            return L"This mode briefly suspends each target thread and is "
                   L"not a kernel on-CPU or PMU profiler.";
    }
}

static VOID
RperfUpdateSessionSummary(VOID)
{
    WCHAR Summary[4096];
    LRESULT ThreadItems = SendMessageW(App.ThreadCombo, CB_GETCOUNT, 0, 0);
    SIZE_T ThreadCount = ThreadItems > 0 ? (SIZE_T)ThreadItems - 1 : 0;
    PCWSTR ViewScope = L"wall clock (waiting-state samples included)";

    if (App.Session.SampleCount == 0)
    {
        SetWindowTextW(App.SessionSummary, L"No profile is loaded.");
        return;
    }
    if (App.Session.FilterFlags & RPERF_FILTER_CPU_ONLY) ViewScope = L"CPU samples only (waiting-state samples excluded)";

    _snwprintf(
        Summary,
        ARRAYSIZE(Summary),
        L"Target: %s\r\n"
        L"Process ID: %lu\r\n"
        L"Source log: %s\r\n\r\n"
        L"Recorder: %s\r\n"
        L"Interval: %lu ms\r\n"
        L"Requested duration: %lu ms%s\r\n"
        L"Measured elapsed time: %.6f s\r\n"
        L"Completion: %s\r\n"
        L"Complete footer: %s\r\n"
        L"Capture error: %lu\r\n\r\n"
        L"Samples in current view: %Iu of %Iu\r\n"
        L"Threads represented: %Iu\r\n"
        L"View time range: %.6f to %.6f s\r\n"
        L"View thread: %s\r\n"
        L"View scope: %s\r\n"
        L"Scheduler state: %I64u of %Iu samples tagged, %I64u waiting\r\n\r\n"
        L"Capture accounting:\r\n"
        L"Attempted samples: %I64u\r\n"
        L"Accepted samples: %I64u\r\n"
        L"Failed samples: %I64u\r\n"
        L"Skipped samples: %I64u\r\n"
        L"Truncated samples: %I64u\r\n"
        L"Missed cadence ticks: %I64u\r\n"
        L"Lost records: %I64u (weight %I64u)\r\n"
        L"Failure reasons: thread open %I64u, ownership %I64u, "
        L"suspend %I64u, context %I64u, unwind %I64u, resume %I64u\r\n"
        L"Input diagnostics: schema skips %I64u, malformed records %I64u\r\n"
        L"Target user CPU time: %.6f s\r\n"
        L"Target kernel CPU time: %.6f s\r\n\r\n"
        L"Symbolization: %s\r\n\r\n"
        L"Interpretation: hot-function inclusive cost is global across the "
        L"matching samples; flame boxes are contextual call paths. %s",
        App.Session.ProcessName,
        App.Session.ProcessId,
        App.Session.SourcePath,
        RperfBackendSummaryName(App.Session.Backend),
        App.Session.IntervalMs,
        App.Session.RequestedDurationMs,
        App.Session.RequestedDurationMs == 0 ? L" (manual stop)" : L"",
        (double)App.Session.ElapsedUs / 1000000.0,
        RperfCompletionText(App.Session.CompletionReason),
        App.Session.LogComplete ? L"yes" : L"no",
        App.Session.CaptureError,
        App.Session.FilteredSampleCount,
        App.Session.SampleCount,
        ThreadCount,
        (double)App.Session.FilterStartUs / 1000000.0,
        (double)App.Session.FilterEndUs / 1000000.0,
        App.Session.FilterThreadId == 0 ? L"all threads" : L"one thread",
        ViewScope,
        App.Session.StateTaggedSamples,
        App.Session.SampleCount,
        App.Session.WaitingSamples,
        App.Session.Counters.AttemptedSamples,
        App.Session.Counters.SuccessfulSamples,
        App.Session.Counters.FailedSamples,
        App.Session.Counters.SkippedSamples,
        App.Session.Counters.TruncatedSamples,
        App.Session.Counters.MissedCadenceTicks,
        App.Session.Counters.LostRecords,
        App.Session.Counters.LostWeight,
        App.Session.Counters.ThreadOpenFailures,
        App.Session.Counters.ThreadOwnershipFailures,
        App.Session.Counters.SuspendFailures,
        App.Session.Counters.ContextFailures,
        App.Session.Counters.UnwindFailures,
        App.Session.Counters.ResumeFailures,
        App.Session.Counters.SchemaSkips,
        App.Session.Counters.MalformedRecords,
        (double)App.Session.UserTime100ns / 10000000.0,
        (double)App.Session.KernelTime100ns / 10000000.0,
        App.HasSymbolSummary ?
            L"PDB/embedded rsym/DWARF/COFF/export resolution was attempted; "
            L"exact counts and rejected identity mismatches are shown in "
            L"the status bar." :
            L"not attempted for this in-memory capture",
        RperfBackendSummaryCaveat(App.Session.Backend));
    Summary[ARRAYSIZE(Summary) - 1] = UNICODE_NULL;
    SetWindowTextW(App.SessionSummary, Summary);
}

static VOID
RperfRefreshAnalysisViews(VOID)
{
    RPERF_TIME_RANGE Range;

    RperfPopulateFunctions();
    SendMessageW(App.FlameGraph,
                 WM_RPERF_SET_SESSION,
                 0,
                 (LPARAM)&App.Session);
    Range.StartUs = App.Session.FilterStartUs;
    Range.EndUs = App.Session.FilterEndUs;
    SendMessageW(App.Timeline,
                 WM_RPERF_TIMELINE_SET_RANGE,
                 0,
                 (LPARAM)&Range);
    RperfUpdateSessionSummary();
    RperfSetSessionStatus();
}

static DWORD
RperfFilterFlagsFromControls(VOID)
{
    if (SendMessageW(App.CpuOnlyCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) return RPERF_FILTER_CPU_ONLY;
    return 0;
}

static BOOL
RperfApplyFilterValues(DWORD ThreadId,
                       ULONGLONG StartUs,
                       ULONGLONG EndUs,
                       DWORD FilterFlags)
{
    if (!RperfBuildFilteredAnalysis(&App.Session, ThreadId, StartUs, EndUs, FilterFlags))
    {
        RperfShowSystemError(App.MainWindow,
                             L"Building the filtered profile",
                             ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    RperfSetTimeControls(StartUs, EndUs);
    RperfRefreshAnalysisViews();
    return TRUE;
}

static VOID
RperfApplyFiltersFromControls(VOID)
{
    LRESULT Selection, Data;
    DWORD ThreadId;
    ULONGLONG StartUs, EndUs;

    if (App.Session.SampleCount == 0)
        return;
    Selection = SendMessageW(App.ThreadCombo, CB_GETCURSEL, 0, 0);
    Data = Selection != CB_ERR ?
           SendMessageW(App.ThreadCombo, CB_GETITEMDATA, Selection, 0) : 0;
    ThreadId = Data == CB_ERR ? 0 : (DWORD)(ULONG_PTR)Data;
    if (!RperfParseSeconds(App.TimeStartEdit, &StartUs) ||
        !RperfParseSeconds(App.TimeEndEdit, &EndUs) ||
        StartUs > EndUs || EndUs > App.Session.ElapsedUs)
    {
        MessageBoxW(App.MainWindow,
                    L"Enter a valid time range inside the captured duration "
                    L"(up to six decimal places).",
                    App.Title,
                    MB_OK | MB_ICONWARNING);
        return;
    }
    RperfApplyFilterValues(ThreadId, StartUs, EndUs, RperfFilterFlagsFromControls());
}

static VOID
RperfResetFilters(VOID)
{
    if (App.Session.SampleCount == 0)
        return;
    SetWindowTextW(App.SearchEdit, L"");
    SendMessageW(App.ThreadCombo, CB_SETCURSEL, 0, 0);
    SendMessageW(App.CpuOnlyCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(App.FlameGraph, WM_RPERF_SET_SEARCH, 0, (LPARAM)L"");
    RperfApplyFilterValues(0, 0, App.Session.ElapsedUs, 0);
}

static VOID
RperfUpdateSearch(VOID)
{
    WCHAR Search[128];

    GetWindowTextW(App.SearchEdit, Search, ARRAYSIZE(Search));
    SendMessageW(App.FlameGraph,
                 WM_RPERF_SET_SEARCH,
                 0,
                 (LPARAM)Search);
    RperfPopulateFunctions();
}

static VOID
RperfShowSession(VOID)
{
    WCHAR WindowTitle[MAX_PATH + 160];
    WPARAM CpuOnlyState;

    SetWindowTextW(App.SearchEdit, L"");
    RperfPopulateThreadFilter();
    CpuOnlyState = BST_UNCHECKED;
    if (App.Session.FilterFlags & RPERF_FILTER_CPU_ONLY) CpuOnlyState = BST_CHECKED;
    SendMessageW(App.CpuOnlyCheck, BM_SETCHECK, CpuOnlyState, 0);
    RperfSetTimeControls(App.Session.FilterStartUs,
                         App.Session.FilterEndUs);
    RperfPopulateFunctions();
    SendMessageW(App.FlameGraph,
                 WM_RPERF_SET_SESSION,
                 0,
                 (LPARAM)&App.Session);
    SendMessageW(App.FlameGraph, WM_RPERF_SET_SEARCH, 0, (LPARAM)L"");
    SendMessageW(App.Timeline,
                 WM_RPERF_TIMELINE_SET_SESSION,
                 0,
                 (LPARAM)&App.Session);
    RperfUpdateSessionSummary();
    _snwprintf(WindowTitle,
               ARRAYSIZE(WindowTitle),
               L"%s - %s",
               App.Title,
               App.Session.ProcessName[0] ?
                   App.Session.ProcessName : L"profile");
    WindowTitle[ARRAYSIZE(WindowTitle) - 1] = UNICODE_NULL;
    SetWindowTextW(App.MainWindow, WindowTitle);
    RperfSetSessionStatus();
}

static VOID
RperfSetBusyControls(BOOL Busy,
                     BOOL CanStop)
{
    EnableWindow(App.ProcessCombo, !Busy);
    EnableWindow(App.RefreshButton, !Busy);
    EnableWindow(App.BackendCombo, !Busy);
    EnableWindow(App.IntervalEdit, !Busy);
    EnableWindow(App.DurationEdit, !Busy);
    EnableWindow(App.RecordButton, !Busy);
    EnableWindow(App.LaunchButton, !Busy);
    EnableWindow(App.OpenButton, !Busy);
    EnableWindow(App.StopButton, CanStop);
    EnableWindow(App.SearchEdit, !Busy);
    EnableWindow(App.ThreadCombo, !Busy);
    EnableWindow(App.TimeStartEdit, !Busy);
    EnableWindow(App.TimeEndEdit, !Busy);
    EnableWindow(App.ApplyFilterButton, !Busy);
    EnableWindow(App.ResetFilterButton, !Busy);
    EnableWindow(App.CpuOnlyCheck, !Busy);
    EnableWindow(App.ResetZoomButton, !Busy);
    EnableWindow(App.Tab, !Busy);
    EnableWindow(App.FlameGraph, !Busy);
    EnableWindow(App.FunctionList, !Busy);
    EnableWindow(App.Timeline, !Busy);
    EnableWindow(App.SessionSummary, !Busy);
    EnableMenuItem(GetMenu(App.MainWindow),
                   IDM_CAPTURE_REFRESH,
                   MF_BYCOMMAND | (Busy ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(GetMenu(App.MainWindow),
                   IDM_CAPTURE_RECORD,
                   MF_BYCOMMAND | (Busy ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(GetMenu(App.MainWindow),
                   IDM_CAPTURE_LAUNCH,
                   MF_BYCOMMAND | (Busy ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(GetMenu(App.MainWindow),
                   IDM_FILE_OPEN,
                   MF_BYCOMMAND | (Busy ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(GetMenu(App.MainWindow),
                   IDM_CAPTURE_STOP,
                   MF_BYCOMMAND | (CanStop ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(GetMenu(App.MainWindow),
                   IDM_VIEW_RESET_ZOOM,
                   MF_BYCOMMAND | (Busy ? MF_GRAYED : MF_ENABLED));
    DrawMenuBar(App.MainWindow);
}

static VOID
RperfSetCapturing(BOOL Capturing)
{
    RperfSetBusyControls(Capturing, Capturing);
}

static VOID
RperfSetProcessing(BOOL Processing)
{
    App.Processing = Processing;
    RperfSetBusyControls(Processing, Processing);
}

static BOOL
RperfChooseLogPath(PWSTR Path,
                   SIZE_T PathCount,
                   BOOL Save)
{
    OPENFILENAMEW Dialog;
    static const WCHAR Filter[] =
        L"ReactOS profile logs (*.rperf)\0*.rperf\0"
        L"All files (*.*)\0*.*\0\0";

    ZeroMemory(&Dialog, sizeof(Dialog));
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = App.MainWindow;
    Dialog.lpstrFilter = Filter;
    Dialog.nFilterIndex = 1;
    Dialog.lpstrFile = Path;
    Dialog.nMaxFile = (DWORD)PathCount;
    Dialog.lpstrDefExt = L"rperf";
    Dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST |
                   (Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return Save ? GetSaveFileNameW(&Dialog) : GetOpenFileNameW(&Dialog);
}

static VOID
RperfClearViews(PCWSTR Status)
{
    SendMessageW(App.FlameGraph, WM_RPERF_SET_SESSION, 0, 0);
    SendMessageW(App.FlameGraph, WM_RPERF_SET_SEARCH, 0, (LPARAM)L"");
    SendMessageW(App.Timeline, WM_RPERF_TIMELINE_SET_SESSION, 0, 0);
    SendMessageW(App.FunctionList, LVM_DELETEALLITEMS, 0, 0);
    SendMessageW(App.ThreadCombo, CB_RESETCONTENT, 0, 0);
    SetWindowTextW(App.SearchEdit, L"");
    SetWindowTextW(App.TimeStartEdit, L"0.000000");
    SetWindowTextW(App.TimeEndEdit, L"0.000000");
    SetWindowTextW(App.SessionSummary, L"");
    SetWindowTextW(App.MainWindow, App.Title);
    RperfSetStatus(Status != NULL ? Status : L"Ready.");
}

static BOOL
RperfReadCaptureSettings(PUINT Interval,
                         PUINT DurationSeconds)
{
    BOOL Converted;

    *Interval = GetDlgItemInt(App.MainWindow,
                              IDC_INTERVAL,
                              &Converted,
                              FALSE);
    if (!Converted || *Interval < 1 || *Interval > 1000)
    {
        MessageBoxW(App.MainWindow,
                    L"The snapshot interval must be between 1 and 1000 ms.",
                    App.Title,
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    *DurationSeconds = GetDlgItemInt(App.MainWindow,
                                     IDC_DURATION,
                                     &Converted,
                                     FALSE);
    if (!Converted || *DurationSeconds > 86400)
    {
        MessageBoxW(App.MainWindow,
                    L"The duration must be 0 (manual stop) through 86400 seconds.",
                    App.Title,
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfIsCapturing(VOID)
{
    return (App.Recorder != NULL) ||
           InterlockedCompareExchange(&App.Session.Capturing, FALSE, FALSE);
}

static BOOL
RperfIsBusy(VOID)
{
    return App.Processing || RperfIsCapturing();
}

static RPERF_BACKEND_KIND
RperfSelectedBackend(VOID)
{
    LRESULT Selection, Data;

    Selection = SendMessageW(App.BackendCombo, CB_GETCURSEL, 0, 0);
    if (Selection == CB_ERR)
        return RperfBackendIntrusive;
    Data = SendMessageW(App.BackendCombo, CB_GETITEMDATA, Selection, 0);
    if (Data == CB_ERR || Data == 0)
        return RperfBackendIntrusive;
    return (RPERF_BACKEND_KIND)Data;
}

static DWORD WINAPI
RperfRecorderMonitorThread(PVOID Parameter)
{
    RPERF_RECORDER *Recorder = Parameter;
    RPERF_RECORDING *Recording;
    DWORD Error = ERROR_SUCCESS;

    for (;;)
    {
        if (RperfRecorderJoin(Recorder, 1000))
            break;
        if (GetLastError() != ERROR_TIMEOUT)
        {
            PostMessageW(App.MainWindow,
                         WM_RPERF_CAPTURE_DONE,
                         GetLastError(),
                         0);
            return 0;
        }
        {
            RPERF_CAPTURE_COUNTERS Counters;
            if (RperfRecorderGetCounters(Recorder, &Counters))
            {
                PostMessageW(App.MainWindow,
                             WM_RPERF_CAPTURE_PROGRESS,
                             (WPARAM)(SIZE_T)Counters.SuccessfulSamples,
                             (LPARAM)(ULONG_PTR)Counters.FailedSamples);
            }
        }
    }

    Recording = RperfRecorderTakeRecording(Recorder);
    if (Recording == NULL)
    {
        Error = GetLastError();
        if (App.RecorderStream != NULL) RperfCodecStreamAbort(App.RecorderStream);
        App.RecorderStream = NULL;
    }
    else
    {
        BOOL Saved = FALSE;

        if (App.RecorderStream != NULL)
        {
            Saved = RperfCodecStreamFinalize(App.RecorderStream, Recording);
            App.RecorderStream = NULL;
        }
        /* A failed or absent stream falls back to the whole-file save so a
         * capture is never lost to a streaming error. */
        if (!Saved && !RperfCodecSave(App.RecorderLogPath, RperfCodecV2Binary, Recording, NULL, NULL, NULL))
        {
            Error = GetLastError();
        }
        RperfRecordingRelease(Recording);
    }
    PostMessageW(App.MainWindow, WM_RPERF_CAPTURE_DONE, Error, 0);
    return 0;
}

static VOID
RperfFinishRecorderCapture(VOID)
{
    if (App.RecorderMonitor != NULL)
    {
        WaitForSingleObject(App.RecorderMonitor, INFINITE);
        CloseHandle(App.RecorderMonitor);
        App.RecorderMonitor = NULL;
    }
    if (App.Recorder != NULL)
    {
        RperfRecorderDestroy(App.Recorder);
        App.Recorder = NULL;
    }
}

static BOOL
RperfBeginRecorderCapture(RPERF_BACKEND_KIND Backend,
                          DWORD ProcessId,
                          PCWSTR ProcessName,
                          UINT Interval,
                          UINT DurationSeconds,
                          PCWSTR LogPath)
{
    RPERF_CAPTURE_CONFIGURATION Config;
    RPERF_RECORDER_CAPABILITIES Capabilities;

    RperfInitializeCaptureConfiguration(&Config);
    Config.Backend = Backend;
    Config.Scope = RperfScopeProcess;
    Config.ProcessId = ProcessId;
    Config.IntervalUs = Interval * 1000;
    Config.DurationMs = DurationSeconds * 1000;
    Config.TargetName = ProcessName;
    Config.OutputPath = LogPath;
    Config.LegacyNotifyWindow = App.MainWindow;
    if (RperfRecorderQueryCapabilities(Backend, &Capabilities) &&
        (Capabilities.Features & RPERF_CAP_KERNEL_STACKS) != 0)
    {
        Config.IncludeKernel = TRUE;
    }

    wcsncpy(App.RecorderLogPath, LogPath, ARRAYSIZE(App.RecorderLogPath));
    App.RecorderLogPath[ARRAYSIZE(App.RecorderLogPath) - 1] = UNICODE_NULL;

    /* Stream records to the log while the capture runs; if the stream cannot
     * be opened the capture still works through the whole-file save. */
    App.RecorderStream = RperfCodecStreamOpen(App.RecorderLogPath, &Config.Limits);
    if (App.RecorderStream != NULL)
    {
        Config.RecordSink = RperfCodecStreamSink;
        Config.RecordSinkContext = App.RecorderStream;
    }

    if (!RperfRecorderCreate(&Config, &App.Recorder))
    {
        DWORD Error = GetLastError();
        if (App.RecorderStream != NULL) RperfCodecStreamAbort(App.RecorderStream);
        App.RecorderStream = NULL;
        if (Capabilities.Description[0] != UNICODE_NULL)
        {
            WCHAR Message[512];
            _snwprintf(Message,
                       ARRAYSIZE(Message),
                       L"The selected recorder cannot start this capture.\n\n%s",
                       Capabilities.Description);
            Message[ARRAYSIZE(Message) - 1] = UNICODE_NULL;
            MessageBoxW(App.MainWindow,
                        Message,
                        App.Title,
                        MB_OK | MB_ICONWARNING);
        }
        SetLastError(Error);
        return FALSE;
    }
    if (!RperfRecorderStart(App.Recorder))
    {
        DWORD Error = GetLastError();
        RperfRecorderDestroy(App.Recorder);
        App.Recorder = NULL;
        if (App.RecorderStream != NULL) RperfCodecStreamAbort(App.RecorderStream);
        App.RecorderStream = NULL;
        SetLastError(Error);
        return FALSE;
    }
    App.RecorderMonitor = CreateThread(NULL,
                                       0,
                                       RperfRecorderMonitorThread,
                                       App.Recorder,
                                       0,
                                       NULL);
    if (App.RecorderMonitor == NULL)
    {
        DWORD Error = GetLastError();
        RperfRecorderRequestStop(App.Recorder);
        RperfRecorderDestroy(App.Recorder);
        App.Recorder = NULL;
        if (App.RecorderStream != NULL) RperfCodecStreamAbort(App.RecorderStream);
        App.RecorderStream = NULL;
        SetLastError(Error);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfBeginRecording(DWORD ProcessId,
                    PCWSTR ProcessName,
                    UINT Interval,
                    UINT DurationSeconds,
                    PCWSTR LogPath)
{
    RPERF_BACKEND_KIND Backend = RperfSelectedBackend();
    BOOL Started;

    RperfSetCapturing(TRUE);
    RperfClearViews(L"Starting the recorder...");
    if (Backend == RperfBackendIntrusive)
    {
        Started = RperfCaptureStart(&App.Session,
                                    ProcessId,
                                    ProcessName,
                                    Interval,
                                    DurationSeconds * 1000,
                                    LogPath,
                                    App.MainWindow);
    }
    else
    {
        Started = RperfBeginRecorderCapture(Backend,
                                            ProcessId,
                                            ProcessName,
                                            Interval,
                                            DurationSeconds,
                                            LogPath);
    }
    if (!Started)
    {
        DWORD Error = GetLastError();
        RperfSetCapturing(FALSE);
        RperfClearViews(L"The recorder could not be started.");
        RperfShowSystemError(App.MainWindow,
                             L"Starting the recorder",
                             Error);
        return FALSE;
    }

    switch (Backend)
    {
        case RperfBackendKernel:
            RperfSetStatus(L"Recording kernel on-CPU samples through the RosProf device...");
            break;
        case RperfBackendEtw:
            RperfSetStatus(L"Recording through the documented ETW sampled-profile backend...");
            break;
        case RperfBackendFake:
            RperfSetStatus(L"Recording deterministic synthetic contract-test samples...");
            break;
        default:
            RperfSetStatus(L"Recording intrusive all-thread wall-clock stack snapshots...");
            break;
    }
    return TRUE;
}

static VOID
RperfStartRecording(VOID)
{
    const RPERF_PROCESS_INFO *Process = RperfGetSelectedProcess();
    UINT Interval, DurationSeconds;
    WCHAR LogPath[MAX_PATH] = L"profile.rperf";

    if (Process == NULL)
    {
        MessageBoxW(App.MainWindow,
                    L"Select a process to record.",
                    App.Title,
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!RperfReadCaptureSettings(&Interval, &DurationSeconds))
        return;

    if (!RperfChooseLogPath(LogPath, ARRAYSIZE(LogPath), TRUE))
        return;

    RperfBeginRecording(Process->ProcessId,
                        Process->Name,
                        Interval,
                        DurationSeconds,
                        LogPath);
}

static INT_PTR CALLBACK
RperfLaunchDlgProc(HWND Dialog,
                   UINT Message,
                   WPARAM WParam,
                   LPARAM LParam)
{
    UNREFERENCED_PARAMETER(LParam);

    switch (Message)
    {
        case WM_INITDIALOG:
            SetDlgItemTextW(Dialog, IDC_LAUNCH_PATH, App.LaunchPath);
            SetDlgItemTextW(Dialog,
                            IDC_LAUNCH_ARGUMENTS,
                            App.LaunchArguments);
            SetDlgItemTextW(Dialog,
                            IDC_LAUNCH_DIRECTORY,
                            App.LaunchDirectory);
            SetFocus(GetDlgItem(Dialog, IDC_LAUNCH_PATH));
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(WParam))
            {
                case IDC_LAUNCH_BROWSE:
                {
                    OPENFILENAMEW Open;
                    WCHAR Path[MAX_PATH] = L"";
                    static const WCHAR Filter[] =
                        L"Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0\0";

                    GetDlgItemTextW(Dialog,
                                    IDC_LAUNCH_PATH,
                                    Path,
                                    ARRAYSIZE(Path));
                    ZeroMemory(&Open, sizeof(Open));
                    Open.lStructSize = sizeof(Open);
                    Open.hwndOwner = Dialog;
                    Open.lpstrFilter = Filter;
                    Open.lpstrFile = Path;
                    Open.nMaxFile = ARRAYSIZE(Path);
                    Open.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST |
                                 OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&Open))
                    {
                        SetDlgItemTextW(Dialog, IDC_LAUNCH_PATH, Path);
                        if (GetWindowTextLengthW(
                                GetDlgItem(Dialog,
                                           IDC_LAUNCH_DIRECTORY)) == 0)
                        {
                            PWSTR Separator = wcsrchr(Path, L'\\');
                            if (Separator == NULL)
                                Separator = wcsrchr(Path, L'/');
                            if (Separator != NULL)
                            {
                                *Separator = UNICODE_NULL;
                                SetDlgItemTextW(Dialog,
                                                IDC_LAUNCH_DIRECTORY,
                                                Path);
                            }
                        }
                    }
                    return TRUE;
                }

                case IDOK:
                    GetDlgItemTextW(Dialog,
                                    IDC_LAUNCH_PATH,
                                    App.LaunchPath,
                                    ARRAYSIZE(App.LaunchPath));
                    GetDlgItemTextW(Dialog,
                                    IDC_LAUNCH_ARGUMENTS,
                                    App.LaunchArguments,
                                    ARRAYSIZE(App.LaunchArguments));
                    GetDlgItemTextW(Dialog,
                                    IDC_LAUNCH_DIRECTORY,
                                    App.LaunchDirectory,
                                    ARRAYSIZE(App.LaunchDirectory));
                    if (App.LaunchPath[0] == UNICODE_NULL)
                    {
                        MessageBoxW(Dialog,
                                    L"Choose an application to launch.",
                                    App.Title,
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    EndDialog(Dialog, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(Dialog, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

static VOID
RperfLaunchAndRecord(VOID)
{
    UINT Interval, DurationSeconds;
    WCHAR LogPath[MAX_PATH] = L"profile.rperf";
    WCHAR CommandLine[4096];
    PCWSTR ProcessName;
    STARTUPINFOW Startup;
    PROCESS_INFORMATION Process;

    if (!RperfReadCaptureSettings(&Interval, &DurationSeconds))
        return;
    if (DialogBoxParamW(App.Instance,
                        MAKEINTRESOURCEW(IDD_LAUNCH),
                        App.MainWindow,
                        RperfLaunchDlgProc,
                        0) != IDOK)
    {
        return;
    }
    if (!RperfChooseLogPath(LogPath, ARRAYSIZE(LogPath), TRUE))
        return;

    _snwprintf(CommandLine,
               ARRAYSIZE(CommandLine),
               L"\"%s\"%s%s",
               App.LaunchPath,
               App.LaunchArguments[0] ? L" " : L"",
               App.LaunchArguments);
    CommandLine[ARRAYSIZE(CommandLine) - 1] = UNICODE_NULL;
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    ZeroMemory(&Process, sizeof(Process));
    if (!CreateProcessW(App.LaunchPath,
                        CommandLine,
                        NULL,
                        NULL,
                        FALSE,
                        CREATE_SUSPENDED,
                        NULL,
                        App.LaunchDirectory[0] ? App.LaunchDirectory : NULL,
                        &Startup,
                        &Process))
    {
        RperfShowSystemError(App.MainWindow,
                             L"Launching the target process",
                             GetLastError());
        return;
    }

    ProcessName = wcsrchr(App.LaunchPath, L'\\');
    if (ProcessName == NULL)
        ProcessName = wcsrchr(App.LaunchPath, L'/');
    ProcessName = ProcessName != NULL ? ProcessName + 1 : App.LaunchPath;
    if (!RperfBeginRecording(Process.dwProcessId,
                             ProcessName,
                             Interval,
                             DurationSeconds,
                             LogPath))
    {
        TerminateProcess(Process.hProcess, ERROR_CANCELLED);
    }
    else
    {
        if (ResumeThread(Process.hThread) == (DWORD)-1)
        {
            DWORD Error = GetLastError();
            if (App.Recorder != NULL)
                RperfRecorderRequestStop(App.Recorder);
            else
                RperfCaptureStop(&App.Session);
            TerminateProcess(Process.hProcess, Error);
            RperfShowSystemError(App.MainWindow,
                                 L"Starting the launched process",
                                 Error);
        }
    }
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}

static BOOL
RperfOfflinePathIsNetwork(PCWSTR Path)
{
    return Path != NULL &&
           ((Path[0] == L'\\' && Path[1] == L'\\') ||
            (Path[0] == L'/' && Path[1] == L'/'));
}

static BOOL
RperfAppendOfflineRoot(PWSTR SearchPath,
                       SIZE_T SearchPathCount,
                       PCWSTR Root)
{
    SIZE_T Used, Length;

    if (Root == NULL || Root[0] == UNICODE_NULL)
        return TRUE;
    if (wcschr(Root, L';') != NULL)
        return TRUE;
    Used = wcslen(SearchPath);
    Length = wcslen(Root);
    if (Length > SearchPathCount - 1 ||
        Used > SearchPathCount - Length - 1 - (Used != 0 ? 1 : 0))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (Used != 0)
        SearchPath[Used++] = L';';
    CopyMemory(SearchPath + Used, Root, (Length + 1) * sizeof(WCHAR));
    return TRUE;
}

static BOOL
RperfAppendOfflineChild(PWSTR SearchPath,
                        SIZE_T SearchPathCount,
                        PCWSTR Root,
                        PCWSTR Child)
{
    WCHAR Path[MAX_PATH * 2];
    SIZE_T RootLength, ChildLength;
    BOOL Separator;

    if (Root == NULL || Root[0] == UNICODE_NULL)
        return TRUE;
    RootLength = wcslen(Root);
    ChildLength = wcslen(Child);
    Separator = Root[RootLength - 1] != L'\\' &&
                Root[RootLength - 1] != L'/';
    if (RootLength + ChildLength + (Separator ? 2 : 1) > ARRAYSIZE(Path))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    CopyMemory(Path, Root, RootLength * sizeof(WCHAR));
    if (Separator)
        Path[RootLength++] = L'\\';
    CopyMemory(Path + RootLength, Child,
               (ChildLength + 1) * sizeof(WCHAR));
    return RperfAppendOfflineRoot(SearchPath, SearchPathCount, Path);
}

static BOOL
RperfGetLogDirectory(PCWSTR LogPath,
                     PWSTR Directory,
                     SIZE_T DirectoryCount)
{
    PWSTR Slash, Backslash;

    if (LogPath == NULL || wcslen(LogPath) >= DirectoryCount)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    lstrcpyW(Directory, LogPath);
    Slash = wcsrchr(Directory, L'/');
    Backslash = wcsrchr(Directory, L'\\');
    if (Slash == NULL || (Backslash != NULL && Backslash > Slash))
        Slash = Backslash;
    if (Slash != NULL)
        *Slash = UNICODE_NULL;
    else
        lstrcpyW(Directory, L".");
    return TRUE;
}

static BOOL
RperfBuildOfflineSearchPaths(PCWSTR LogPath,
                             PWSTR ImagePath,
                             SIZE_T ImagePathCount,
                             PWSTR SymbolPath,
                             SIZE_T SymbolPathCount)
{
    WCHAR Directory[MAX_PATH];
    WCHAR WindowsDirectory[MAX_PATH];
    WCHAR SystemDirectory[MAX_PATH];
    UINT Length;

    ImagePath[0] = UNICODE_NULL;
    SymbolPath[0] = UNICODE_NULL;
    if (!RperfGetLogDirectory(LogPath, Directory, ARRAYSIZE(Directory)))
        return FALSE;
    if (!RperfOfflinePathIsNetwork(Directory))
    {
        if (!RperfAppendOfflineRoot(ImagePath, ImagePathCount, Directory) ||
            !RperfAppendOfflineChild(ImagePath, ImagePathCount,
                                     Directory, L"images") ||
            !RperfAppendOfflineRoot(SymbolPath, SymbolPathCount, Directory) ||
            !RperfAppendOfflineChild(SymbolPath, SymbolPathCount,
                                     Directory, L"symbols"))
            return FALSE;
    }
    Length = GetWindowsDirectoryW(WindowsDirectory,
                                  ARRAYSIZE(WindowsDirectory));
    if (Length != 0 && Length < ARRAYSIZE(WindowsDirectory))
    {
        if (!RperfAppendOfflineRoot(ImagePath, ImagePathCount,
                                    WindowsDirectory) ||
            !RperfAppendOfflineChild(SymbolPath, SymbolPathCount,
                                     WindowsDirectory, L"symbols"))
            return FALSE;
    }
    Length = GetSystemDirectoryW(SystemDirectory,
                                 ARRAYSIZE(SystemDirectory));
    if (Length != 0 && Length < ARRAYSIZE(SystemDirectory))
    {
        if (!RperfAppendOfflineRoot(ImagePath, ImagePathCount,
                                    SystemDirectory) ||
            !RperfAppendOfflineChild(ImagePath, ImagePathCount,
                                     SystemDirectory, L"drivers") ||
            !RperfAppendOfflineRoot(SymbolPath, SymbolPathCount,
                                    SystemDirectory))
            return FALSE;
    }
    return TRUE;
}

static VOID CALLBACK
RperfOpenJobProgress(PVOID Context,
                     ULONGLONG Generation,
                     RPERF_JOB_KIND Kind,
                     ULONGLONG Completed,
                     ULONGLONG Total)
{
    ULONG Percent = Total != 0 ?
                    (Completed >= Total ? 100 :
                     (ULONG)(((double)Completed * 100.0) /
                             (double)Total)) : 0;

    UNREFERENCED_PARAMETER(Generation);
    PostMessageW((HWND)Context, WM_RPERF_JOB_PROGRESS,
                 (WPARAM)Kind, (LPARAM)Percent);
}

static VOID CALLBACK
RperfOpenJobComplete(PVOID Context,
                     ULONGLONG Generation,
                     RPERF_JOB_KIND Kind,
                     DWORD Status)
{
    UNREFERENCED_PARAMETER(Generation);
    PostMessageW((HWND)Context, WM_RPERF_JOB_COMPLETE,
                 (WPARAM)Kind, (LPARAM)Status);
}

static VOID
RperfShowLoadedLogWarnings(VOID)
{
    if (App.HasSymbolSummary &&
        App.SymbolSummary.IdentityMismatch != 0)
    {
        MessageBoxW(App.MainWindow,
                    L"One or more image/PDB identities did not match the "
                    L"recorded module identity. Those symbols were rejected "
                    L"and are shown as module+offset.",
                    App.Title,
                    MB_OK | MB_ICONWARNING);
    }
    if (!App.Session.LogComplete)
    {
        MessageBoxW(App.MainWindow,
                    L"The log has no valid completion footer. Complete "
                    L"samples were recovered, but final loss and timing "
                    L"counters may be incomplete.",
                    App.Title,
                    MB_OK | MB_ICONWARNING);
    }
}

static BOOL
RperfCompleteControllerPresentation(VOID)
{
    RPERF_SESSION *PreparedSession;
    DWORD Error;

    PreparedSession =
        RperfControllerTakePreparedSession(&App.Controller);
    if (PreparedSession == NULL)
    {
        Error = GetLastError();
        RperfSetProcessing(FALSE);
        RperfShowSystemError(App.MainWindow,
                             L"Preparing the profile views",
                             Error != ERROR_SUCCESS ? Error : ERROR_NO_DATA);
        return FALSE;
    }
    RperfSessionClear(&App.Session);
    App.Session = *PreparedSession;
    HeapFree(GetProcessHeap(), 0, PreparedSession);
    App.HasSymbolSummary = App.JobHasSymbolSummary;
    App.SymbolSummary = App.JobSymbolSummary;
    RperfShowSession();
    RperfSetProcessing(FALSE);
    if (App.JobSymbolError != ERROR_SUCCESS)
    {
        WCHAR Status[256];
        _snwprintf(Status, ARRAYSIZE(Status),
                   L"Profile opened, but offline symbols were unavailable "
                   L"(error %lu); unresolved frames use module+offset.",
                   App.JobSymbolError);
        Status[ARRAYSIZE(Status) - 1] = UNICODE_NULL;
        RperfSetStatus(Status);
    }
    RperfShowLoadedLogWarnings();
    return TRUE;
}

static BOOL
RperfBeginControllerPresentation(DWORD SymbolError)
{
    DWORD Error;

    App.JobSymbolError = SymbolError;
    if (!RperfControllerBeginPrepareLegacy(&App.Controller,
                                           App.JobPath,
                                           RperfOpenJobProgress,
                                           RperfOpenJobComplete,
                                           App.MainWindow,
                                           &App.JobGeneration))
    {
        Error = GetLastError();
        RperfSetProcessing(FALSE);
        RperfShowSystemError(App.MainWindow,
                             L"Preparing the profile views",
                             Error != ERROR_SUCCESS ? Error :
                                                      ERROR_GEN_FAILURE);
        return FALSE;
    }
    App.JobKind = RperfJobPrepareLegacy;
    RperfSetStatus(L"Building flame graph, timeline, and function views...");
    return TRUE;
}

static BOOL
RperfBeginLoadedLogSymbolization(VOID)
{
    RPERF_DBGHELP_CONFIGURATION Configuration;
    RPERF_CAPTURE_LIMITS Limits;
    WCHAR ImagePath[RPERF_OFFLINE_PATH_CHARS];
    WCHAR SymbolPath[RPERF_OFFLINE_PATH_CHARS];
    DWORD Error;

    if (!RperfBuildOfflineSearchPaths(App.JobPath,
                                      ImagePath, ARRAYSIZE(ImagePath),
                                      SymbolPath, ARRAYSIZE(SymbolPath)))
        return RperfBeginControllerPresentation(GetLastError());
    RperfDefaultCaptureLimits(&Limits);
    ZeroMemory(&Configuration, sizeof(Configuration));
    Configuration.ImageSearchPath = ImagePath;
    Configuration.SymbolSearchPath = SymbolPath;
    Configuration.MaximumCacheEntries = Limits.MaxSymbols;
    App.JobSymbolProvider =
        RperfCreateDbgHelpSymbolProvider(&Configuration);
    if (App.JobSymbolProvider == NULL)
        return RperfBeginControllerPresentation(GetLastError());
    if (!RperfControllerBeginSymbolize(&App.Controller,
                                       App.JobSymbolProvider,
                                       RperfOpenJobProgress,
                                       RperfOpenJobComplete,
                                       App.MainWindow,
                                       &App.JobGeneration))
    {
        Error = GetLastError();
        RperfDestroySymbolProvider(App.JobSymbolProvider);
        App.JobSymbolProvider = NULL;
        return RperfBeginControllerPresentation(Error);
    }
    App.JobKind = RperfJobSymbolize;
    RperfSetStatus(L"Resolving PDB, embedded rsym, and local image symbols...");
    return TRUE;
}

static BOOL
RperfOpenLogPath(PCWSTR Path)
{
    RPERF_CAPTURE_LIMITS Limits;

    if (Path == NULL || Path[0] == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (RperfIsBusy())
    {
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    if (wcslen(Path) >= ARRAYSIZE(App.JobPath))
    {
        RperfShowSystemError(App.MainWindow,
                             L"Opening the profile log",
                             ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    lstrcpyW(App.JobPath, Path);
    App.JobHasSymbolSummary = FALSE;
    ZeroMemory(&App.JobSymbolSummary, sizeof(App.JobSymbolSummary));
    RperfDefaultCaptureLimits(&Limits);
    if (!RperfControllerBeginOpen(&App.Controller,
                                  Path,
                                  &Limits,
                                  RperfOpenJobProgress,
                                  RperfOpenJobComplete,
                                  App.MainWindow,
                                  &App.JobGeneration))
    {
        RperfShowSystemError(App.MainWindow,
                             L"Opening the profile log",
                             GetLastError());
        return FALSE;
    }
    App.JobKind = RperfJobOpen;
    RperfSetProcessing(TRUE);
    RperfSetStatus(L"Opening and validating the profile log...");
    return TRUE;
}

static VOID
RperfOpenLog(VOID)
{
    WCHAR Path[MAX_PATH] = L"";

    if (!RperfChooseLogPath(Path, ARRAYSIZE(Path), FALSE))
        return;
    RperfOpenLogPath(Path);
}

static BOOL
RperfGetStartupLogPath(PCWSTR CommandLine,
                       PWSTR Path,
                       SIZE_T PathCount)
{
    PCWSTR Start, End, Tail;
    SIZE_T Length;

    Start = CommandLine;
    while (*Start == L' ' || *Start == L'\t')
        ++Start;
    if (*Start == UNICODE_NULL)
        return FALSE;

    if (*Start == L'"')
    {
        ++Start;
        End = wcschr(Start, L'"');
        if (End == NULL)
            return FALSE;
        Tail = End + 1;
        while (*Tail == L' ' || *Tail == L'\t')
            ++Tail;
        if (*Tail != UNICODE_NULL)
            return FALSE;
    }
    else
    {
        End = Start + wcslen(Start);
        while (End > Start && (End[-1] == L' ' || End[-1] == L'\t'))
            --End;
    }

    Length = (SIZE_T)(End - Start);
    if (Length == 0 || Length >= PathCount)
        return FALSE;
    CopyMemory(Path, Start, Length * sizeof(*Path));
    Path[Length] = UNICODE_NULL;
    return TRUE;
}

static HMENU
RperfCreateMainMenu(VOID)
{
    HMENU Menu = CreateMenu();
    HMENU File = CreatePopupMenu();
    HMENU Capture = CreatePopupMenu();
    HMENU View = CreatePopupMenu();
    HMENU Help = CreatePopupMenu();

    AppendMenuW(File, MF_STRING, IDM_FILE_OPEN, L"&Open profile log...\tCtrl+O");
    AppendMenuW(File, MF_SEPARATOR, 0, NULL);
    AppendMenuW(File, MF_STRING, IDM_FILE_EXIT, L"E&xit");
    AppendMenuW(Capture, MF_STRING, IDM_CAPTURE_REFRESH, L"&Refresh processes\tF5");
    AppendMenuW(Capture, MF_STRING, IDM_CAPTURE_RECORD, L"Record &selected...\tCtrl+R");
    AppendMenuW(Capture, MF_STRING, IDM_CAPTURE_LAUNCH, L"&Launch and record...\tCtrl+L");
    AppendMenuW(Capture, MF_STRING | MF_GRAYED, IDM_CAPTURE_STOP, L"&Stop");
    AppendMenuW(View, MF_STRING, IDM_VIEW_RESET_ZOOM, L"&Reset flame graph zoom");
    AppendMenuW(Help, MF_STRING, IDM_HELP_ABOUT, L"&About");
    AppendMenuW(Menu, MF_POPUP, (UINT_PTR)File, L"&File");
    AppendMenuW(Menu, MF_POPUP, (UINT_PTR)Capture, L"&Capture");
    AppendMenuW(Menu, MF_POPUP, (UINT_PTR)View, L"&View");
    AppendMenuW(Menu, MF_POPUP, (UINT_PTR)Help, L"&Help");
    return Menu;
}

static HWND
RperfCreateControl(DWORD ExtendedStyle,
                   PCWSTR ClassName,
                   PCWSTR Text,
                   DWORD Style,
                   INT Id)
{
    HWND Control = CreateWindowExW(ExtendedStyle,
                                   ClassName,
                                   Text,
                                   WS_CHILD | WS_VISIBLE | Style,
                                   0,
                                   0,
                                   0,
                                   0,
                                   App.MainWindow,
                                   (HMENU)(INT_PTR)Id,
                                   App.Instance,
                                   NULL);
    if (Control != NULL)
    {
        SendMessageW(Control,
                     WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT),
                     TRUE);
    }
    return Control;
}

static VOID
RperfInitializeFunctionList(VOID)
{
    static const struct
    {
        PCWSTR Name;
        INT Width;
        INT Format;
    } Columns[] =
    {
        { L"Function", 330, LVCFMT_LEFT },
        { L"Module", 135, LVCFMT_LEFT },
        { L"Self", 85, LVCFMT_RIGHT },
        { L"Inclusive", 85, LVCFMT_RIGHT },
        { L"Inclusive %", 95, LVCFMT_RIGHT }
    };
    SIZE_T Index;

    SendMessageW(App.FunctionList,
                 LVM_SETEXTENDEDLISTVIEWSTYLE,
                 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (Index = 0; Index < ARRAYSIZE(Columns); ++Index)
    {
        LVCOLUMNW Column;
        ZeroMemory(&Column, sizeof(Column));
        Column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        Column.pszText = (PWSTR)Columns[Index].Name;
        Column.cx = Columns[Index].Width;
        Column.fmt = Columns[Index].Format;
        SendMessageW(App.FunctionList,
                     LVM_INSERTCOLUMNW,
                     Index,
                     (LPARAM)&Column);
    }
}

static VOID
RperfPopulateBackends(VOID)
{
    static const struct
    {
        RPERF_BACKEND_KIND Kind;
        PCWSTR Name;
    } Choices[] =
    {
        { RperfBackendIntrusive, L"Intrusive wall-clock stacks (userspace)" },
        { RperfBackendKernel,    L"Kernel on-CPU sampling (RosProf)" },
        { RperfBackendEtw,       L"ETW sampled profile (documented APIs)" },
        { RperfBackendFake,      L"Synthetic contract-test recorder" },
    };
    SIZE_T Index;

    SendMessageW(App.BackendCombo, CB_RESETCONTENT, 0, 0);
    for (Index = 0; Index < ARRAYSIZE(Choices); Index++)
    {
        RPERF_RECORDER_CAPABILITIES Capabilities;
        WCHAR Item[288];
        LRESULT Position;
        BOOL Available;

        Available = RperfRecorderQueryCapabilities(Choices[Index].Kind,
                                                   &Capabilities) &&
                    Capabilities.Available;
        _snwprintf(Item,
                   ARRAYSIZE(Item),
                   Available ? L"%s" : L"%s (unavailable)",
                   Choices[Index].Name);
        Item[ARRAYSIZE(Item) - 1] = UNICODE_NULL;
        Position = SendMessageW(App.BackendCombo,
                                CB_ADDSTRING,
                                0,
                                (LPARAM)Item);
        if (Position != CB_ERR)
        {
            SendMessageW(App.BackendCombo,
                         CB_SETITEMDATA,
                         Position,
                         (LPARAM)Choices[Index].Kind);
        }
    }
    SendMessageW(App.BackendCombo, CB_SETCURSEL, 0, 0);
}

static BOOL
RperfCreateControls(VOID)
{
    TCITEMW TabItem;

    App.ProcessLabel = RperfCreateControl(0,
                                         WC_STATICW,
                                         L"Process:",
                                         SS_LEFT,
                                         -1);
    App.ProcessCombo = RperfCreateControl(WS_EX_CLIENTEDGE,
                                         WC_COMBOBOXW,
                                         NULL,
                                         CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                         IDC_PROCESS);
    App.RefreshButton = RperfCreateControl(0,
                                          WC_BUTTONW,
                                          L"Refresh",
                                          BS_PUSHBUTTON | WS_TABSTOP,
                                          IDC_REFRESH);
    App.RecordButton = RperfCreateControl(0,
                                         WC_BUTTONW,
                                         L"Attach...",
                                         BS_DEFPUSHBUTTON | WS_TABSTOP,
                                         IDC_RECORD);
    App.LaunchButton = RperfCreateControl(0,
                                         WC_BUTTONW,
                                         L"Launch...",
                                         BS_PUSHBUTTON | WS_TABSTOP,
                                         IDC_LAUNCH);
    App.StopButton = RperfCreateControl(0,
                                       WC_BUTTONW,
                                       L"Stop",
                                       BS_PUSHBUTTON | WS_TABSTOP,
                                       IDC_STOP);
    App.OpenButton = RperfCreateControl(0,
                                       WC_BUTTONW,
                                       L"Open log...",
                                       BS_PUSHBUTTON | WS_TABSTOP,
                                       IDC_OPEN);
    App.ResetZoomButton = RperfCreateControl(0,
                                            WC_BUTTONW,
                                            L"Reset zoom",
                                            BS_PUSHBUTTON | WS_TABSTOP,
                                            IDC_RESET_ZOOM);
    App.ModeLabel = RperfCreateControl(0,
                                      WC_STATICW,
                                      L"Mode:",
                                      SS_LEFT,
                                      IDC_MODE);
    App.BackendCombo = RperfCreateControl(WS_EX_CLIENTEDGE,
                                         WC_COMBOBOXW,
                                         NULL,
                                         CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                         IDC_BACKEND);
    App.IntervalLabel = RperfCreateControl(0,
                                          WC_STATICW,
                                          L"Interval (ms):",
                                          SS_RIGHT,
                                          -1);
    App.IntervalEdit = RperfCreateControl(WS_EX_CLIENTEDGE,
                                         WC_EDITW,
                                         L"20",
                                         ES_NUMBER | ES_RIGHT | WS_TABSTOP,
                                         IDC_INTERVAL);
    App.DurationLabel = RperfCreateControl(0,
                                          WC_STATICW,
                                          L"Duration (s, 0 = manual):",
                                          SS_RIGHT,
                                          -1);
    App.DurationEdit = RperfCreateControl(WS_EX_CLIENTEDGE,
                                         WC_EDITW,
                                         L"10",
                                         ES_NUMBER | ES_RIGHT | WS_TABSTOP,
                                         IDC_DURATION);
    App.SearchLabel = RperfCreateControl(0,
                                        WC_STATICW,
                                        L"Search:",
                                        SS_LEFT,
                                        -1);
    App.SearchEdit = RperfCreateControl(WS_EX_CLIENTEDGE,
                                       WC_EDITW,
                                       L"",
                                       ES_AUTOHSCROLL | WS_TABSTOP,
                                       IDC_SEARCH);
    App.ThreadLabel = RperfCreateControl(0,
                                        WC_STATICW,
                                        L"Thread:",
                                        SS_RIGHT,
                                        -1);
    App.ThreadCombo = RperfCreateControl(WS_EX_CLIENTEDGE,
                                        WC_COMBOBOXW,
                                        NULL,
                                        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                        IDC_THREAD_FILTER);
    App.TimeLabel = RperfCreateControl(0,
                                      WC_STATICW,
                                      L"Time (s):",
                                      SS_RIGHT,
                                      -1);
    App.TimeStartEdit = RperfCreateControl(WS_EX_CLIENTEDGE,
                                          WC_EDITW,
                                          L"0.000",
                                          ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                          IDC_TIME_START);
    App.TimeEndLabel = RperfCreateControl(0,
                                         WC_STATICW,
                                         L"to",
                                         SS_CENTER,
                                         -1);
    App.TimeEndEdit = RperfCreateControl(WS_EX_CLIENTEDGE,
                                        WC_EDITW,
                                        L"0.000",
                                        ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                        IDC_TIME_END);
    App.ApplyFilterButton = RperfCreateControl(0,
                                              WC_BUTTONW,
                                              L"Apply",
                                              BS_DEFPUSHBUTTON | WS_TABSTOP,
                                              IDC_APPLY_FILTER);
    App.ResetFilterButton = RperfCreateControl(0,
                                              WC_BUTTONW,
                                              L"Reset filters",
                                              BS_PUSHBUTTON | WS_TABSTOP,
                                              IDC_RESET_FILTER);
    App.CpuOnlyCheck = RperfCreateControl(0, WC_BUTTONW, L"CPU only", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_CPU_ONLY);

    App.Tab = RperfCreateControl(0,
                                WC_TABCONTROLW,
                                NULL,
                                WS_CLIPSIBLINGS | WS_TABSTOP,
                                IDC_TAB);
    ZeroMemory(&TabItem, sizeof(TabItem));
    TabItem.mask = TCIF_TEXT;
    TabItem.pszText = L"Flame Graph";
    SendMessageW(App.Tab, TCM_INSERTITEMW, 0, (LPARAM)&TabItem);
    TabItem.pszText = L"Hot Functions";
    SendMessageW(App.Tab, TCM_INSERTITEMW, 1, (LPARAM)&TabItem);
    TabItem.pszText = L"Timeline";
    SendMessageW(App.Tab, TCM_INSERTITEMW, 2, (LPARAM)&TabItem);
    TabItem.pszText = L"Session";
    SendMessageW(App.Tab, TCM_INSERTITEMW, 3, (LPARAM)&TabItem);

    App.FlameGraph = RperfCreateControl(WS_EX_CLIENTEDGE,
                                       RPERF_FLAME_CLASS,
                                       NULL,
                                       WS_TABSTOP,
                                       IDC_FLAME);
    App.FunctionList = RperfCreateControl(WS_EX_CLIENTEDGE,
                                         WC_LISTVIEWW,
                                         NULL,
                                         LVS_REPORT | LVS_SINGLESEL |
                                         LVS_SHOWSELALWAYS | WS_TABSTOP,
                                         IDC_FUNCTIONS);
    RperfInitializeFunctionList();
    ShowWindow(App.FunctionList, SW_HIDE);
    App.Timeline = RperfCreateControl(WS_EX_CLIENTEDGE,
                                     RPERF_TIMELINE_CLASS,
                                     NULL,
                                     WS_TABSTOP,
                                     IDC_TIMELINE);
    ShowWindow(App.Timeline, SW_HIDE);
    App.SessionSummary = RperfCreateControl(WS_EX_CLIENTEDGE,
                                           WC_EDITW,
                                           NULL,
                                           ES_MULTILINE | ES_READONLY |
                                           ES_AUTOVSCROLL | WS_VSCROLL |
                                           WS_TABSTOP,
                                           IDC_SESSION_SUMMARY);
    ShowWindow(App.SessionSummary, SW_HIDE);

    App.StatusBar = CreateWindowExW(0,
                                    STATUSCLASSNAMEW,
                                    L"Ready.",
                                    WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                    0,
                                    0,
                                    0,
                                    0,
                                    App.MainWindow,
                                    (HMENU)(INT_PTR)IDC_STATUS,
                                    App.Instance,
                                    NULL);
    EnableWindow(App.StopButton, FALSE);
    RperfPopulateBackends();
    return App.ProcessCombo != NULL && App.RefreshButton != NULL &&
           App.BackendCombo != NULL &&
           App.IntervalEdit != NULL && App.DurationEdit != NULL &&
           App.RecordButton != NULL && App.LaunchButton != NULL &&
           App.StopButton != NULL &&
           App.OpenButton != NULL && App.Tab != NULL &&
           App.FlameGraph != NULL && App.FunctionList != NULL &&
           App.Timeline != NULL && App.SessionSummary != NULL &&
           App.StatusBar != NULL;
}

static VOID
RperfLayoutControls(HWND Window)
{
    RECT Client, StatusRect, TabClient;
    INT Width, Height, StatusHeight, TabTop = 108;
    INT RightButtons;

    GetClientRect(Window, &Client);
    SendMessageW(App.StatusBar, WM_SIZE, 0, 0);
    GetWindowRect(App.StatusBar, &StatusRect);
    StatusHeight = StatusRect.bottom - StatusRect.top;
    Width = Client.right - Client.left;
    Height = Client.bottom - Client.top;

    MoveWindow(App.ProcessLabel, 8, 11, 54, 20, TRUE);

    RightButtons = Width - 8;
    MoveWindow(App.OpenButton, RightButtons - 86, 7, 86, 26, TRUE);
    RightButtons -= 92;
    MoveWindow(App.StopButton, RightButtons - 64, 7, 64, 26, TRUE);
    RightButtons -= 70;
    MoveWindow(App.LaunchButton, RightButtons - 84, 7, 84, 26, TRUE);
    RightButtons -= 90;
    MoveWindow(App.RecordButton, RightButtons - 82, 7, 82, 26, TRUE);
    RightButtons -= 88;
    MoveWindow(App.RefreshButton, RightButtons - 76, 7, 76, 26, TRUE);
    RightButtons -= 82;
    MoveWindow(App.ProcessCombo, 65, 7, RightButtons - 65, 300, TRUE);

    MoveWindow(App.ModeLabel, 8, 44, 44, 20, TRUE);
    MoveWindow(App.BackendCombo, 56, 41, 282, 320, TRUE);
    MoveWindow(App.IntervalLabel, 350, 43, 78, 20, TRUE);
    MoveWindow(App.DurationLabel, 505, 43, 142, 20, TRUE);
    MoveWindow(App.IntervalEdit, 434, 41, 62, 23, TRUE);
    MoveWindow(App.DurationEdit, 654, 41, 68, 23, TRUE);

    MoveWindow(App.SearchLabel, 8, 77, 42, 20, TRUE);
    MoveWindow(App.SearchEdit, 52, 74, 180, 23, TRUE);
    MoveWindow(App.ThreadLabel, 238, 77, 46, 20, TRUE);
    MoveWindow(App.ThreadCombo, 288, 74, 125, 240, TRUE);
    MoveWindow(App.TimeLabel, 421, 77, 56, 20, TRUE);
    MoveWindow(App.TimeStartEdit, 481, 74, 70, 23, TRUE);
    MoveWindow(App.TimeEndLabel, 555, 77, 20, 20, TRUE);
    MoveWindow(App.TimeEndEdit, 579, 74, 70, 23, TRUE);
    MoveWindow(App.ApplyFilterButton, 656, 73, 58, 25, TRUE);
    MoveWindow(App.ResetFilterButton, 720, 73, 90, 25, TRUE);
    MoveWindow(App.CpuOnlyCheck, 816, 73, 84, 25, TRUE);
    MoveWindow(App.ResetZoomButton, Width - 100, 73, 92, 25, TRUE);

    MoveWindow(App.Tab,
               8,
               TabTop,
               Width - 16,
               Height - TabTop - StatusHeight - 7,
               TRUE);
    GetClientRect(App.Tab, &TabClient);
    SendMessageW(App.Tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&TabClient);
    MapWindowPoints(App.Tab, Window, (POINT *)&TabClient, 2);
    MoveWindow(App.FlameGraph,
               TabClient.left,
               TabClient.top,
               TabClient.right - TabClient.left,
               TabClient.bottom - TabClient.top,
               TRUE);
    MoveWindow(App.FunctionList,
               TabClient.left,
               TabClient.top,
               TabClient.right - TabClient.left,
               TabClient.bottom - TabClient.top,
               TRUE);
    MoveWindow(App.Timeline,
               TabClient.left,
               TabClient.top,
               TabClient.right - TabClient.left,
               TabClient.bottom - TabClient.top,
               TRUE);
    MoveWindow(App.SessionSummary,
               TabClient.left,
               TabClient.top,
               TabClient.right - TabClient.left,
               TabClient.bottom - TabClient.top,
               TRUE);
}

static VOID
RperfSwitchTab(VOID)
{
    INT Selection = (INT)SendMessageW(App.Tab, TCM_GETCURSEL, 0, 0);
    ShowWindow(App.FlameGraph, Selection == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(App.FunctionList, Selection == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(App.Timeline, Selection == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(App.SessionSummary, Selection == 3 ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK
RperfMainWndProc(HWND Window,
                 UINT Message,
                 WPARAM WParam,
                 LPARAM LParam)
{
    switch (Message)
    {
        case WM_CREATE:
            App.MainWindow = Window;
            if (!RperfCreateControls())
                return -1;
            RperfLayoutControls(Window);
            RperfRefreshProcesses();
            return 0;

        case WM_SIZE:
            if (App.StatusBar != NULL)
                RperfLayoutControls(Window);
            return 0;

        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *Info = (MINMAXINFO *)LParam;
            Info->ptMinTrackSize.x = 1000;
            Info->ptMinTrackSize.y = 480;
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(WParam))
            {
                case IDC_REFRESH:
                case IDM_CAPTURE_REFRESH:
                    RperfRefreshProcesses();
                    return 0;
                case IDC_RECORD:
                case IDM_CAPTURE_RECORD:
                    RperfStartRecording();
                    return 0;
                case IDC_LAUNCH:
                case IDM_CAPTURE_LAUNCH:
                    RperfLaunchAndRecord();
                    return 0;
                case IDC_STOP:
                case IDM_CAPTURE_STOP:
                    if (App.Processing)
                    {
                        RperfControllerCancel(&App.Controller);
                        EnableWindow(App.StopButton, FALSE);
                        EnableMenuItem(GetMenu(App.MainWindow),
                                       IDM_CAPTURE_STOP,
                                       MF_BYCOMMAND | MF_GRAYED);
                        RperfSetStatus(L"Canceling the offline analysis safely...");
                    }
                    else if (App.Recorder != NULL)
                        RperfRecorderRequestStop(App.Recorder);
                    else
                        RperfCaptureStop(&App.Session);
                    if (!App.Processing)
                    {
                        EnableWindow(App.StopButton, FALSE);
                        RperfSetStatus(L"Stopping the recorder safely...");
                    }
                    return 0;
                case IDC_OPEN:
                case IDM_FILE_OPEN:
                    RperfOpenLog();
                    return 0;
                case IDC_RESET_ZOOM:
                case IDM_VIEW_RESET_ZOOM:
                    SendMessageW(App.FlameGraph, WM_RPERF_RESET_ZOOM, 0, 0);
                    return 0;
                case IDC_APPLY_FILTER:
                    RperfApplyFiltersFromControls();
                    return 0;
                case IDC_RESET_FILTER:
                    RperfResetFilters();
                    return 0;
                case IDC_CPU_ONLY:
                    if (HIWORD(WParam) == BN_CLICKED && !RperfIsBusy() && App.Session.SampleCount != 0)
                    {
                        RperfApplyFiltersFromControls();
                    }
                    return 0;
                case IDC_SEARCH:
                    if (HIWORD(WParam) == EN_CHANGE && !RperfIsBusy())
                        RperfUpdateSearch();
                    return 0;
                case IDM_FILE_EXIT:
                    SendMessageW(Window, WM_CLOSE, 0, 0);
                    return 0;
                case IDM_HELP_ABOUT:
                    MessageBoxW(Window,
                                L"ReactOS Performance Analyzer\n\n"
                                L"Clean-room profiler with selectable recorder "
                                L"backends and an offline flame graph analyzer.\n\n"
                                L"The default intrusive mode briefly suspends "
                                L"target threads to walk their stacks. Kernel "
                                L"on-CPU sampling through the RosProf device can "
                                L"be selected in the Mode box when available; it "
                                L"records timer samples with bounded kernel-mode "
                                L"call chains where the kernel supports them.",
                                App.Title,
                                MB_OK | MB_ICONINFORMATION);
                    return 0;
            }
            break;

        case WM_NOTIFY:
        {
            NMHDR *Header = (NMHDR *)LParam;
            if (RperfIsBusy())
            {
                return 0;
            }
            if (Header->hwndFrom == App.Tab && Header->code == TCN_SELCHANGE)
            {
                RperfSwitchTab();
                return 0;
            }
            if (Header->hwndFrom == App.FunctionList &&
                Header->code == LVN_COLUMNCLICK)
            {
                NMLISTVIEW *ListView = (NMLISTVIEW *)LParam;

                if (App.SortColumn == ListView->iSubItem)
                    App.SortAscending = !App.SortAscending;
                else
                {
                    App.SortColumn = ListView->iSubItem;
                    App.SortAscending = ListView->iSubItem < 2;
                }
                RperfPopulateFunctions();
                return 0;
            }
            if (Header->hwndFrom == App.FunctionList &&
                Header->code == NM_DBLCLK)
            {
                NMITEMACTIVATE *Activate = (NMITEMACTIVATE *)LParam;

                if (Activate->iItem >= 0)
                {
                    LVITEMW Item;
                    SIZE_T SymbolIndex;

                    ZeroMemory(&Item, sizeof(Item));
                    Item.mask = LVIF_PARAM;
                    Item.iItem = Activate->iItem;
                    if (SendMessageW(App.FunctionList,
                                     LVM_GETITEMW,
                                     0,
                                     (LPARAM)&Item))
                    {
                        SymbolIndex = (SIZE_T)Item.lParam;
                        if (SymbolIndex < App.Session.SymbolCount)
                        {
                            SendMessageW(App.Tab, TCM_SETCURSEL, 0, 0);
                            RperfSwitchTab();
                            SendMessageW(
                                App.FlameGraph,
                                WM_RPERF_ZOOM_ADDRESS,
                                0,
                                (LPARAM)(ULONG_PTR)
                                    App.Session.Symbols[SymbolIndex].
                                        FunctionAddress);
                        }
                    }
                }
                return 0;
            }
            break;
        }

        case WM_RPERF_CAPTURE_PROGRESS:
        {
            WCHAR Status[256];
            if (App.Recorder != NULL)
            {
                PCWSTR Source;

                switch (RperfSelectedBackend())
                {
                    case RperfBackendKernel:
                        Source = L"kernel on-CPU samples";
                        break;
                    case RperfBackendEtw:
                        Source = L"ETW sampled profiles";
                        break;
                    case RperfBackendFake:
                        Source = L"synthetic test samples";
                        break;
                    default:
                        Source = L"samples";
                        break;
                }
                _snwprintf(Status,
                           ARRAYSIZE(Status),
                           L"Recording %s: %Iu accepted, %I64u failed...",
                           Source,
                           (SIZE_T)WParam,
                           (ULONGLONG)(ULONG_PTR)LParam);
            }
            else
            {
                _snwprintf(Status,
                           ARRAYSIZE(Status),
                           L"Recording wall-clock stacks: %Iu samples, %I64u missed thread snapshots...",
                           (SIZE_T)WParam,
                           (ULONGLONG)(ULONG_PTR)LParam);
            }
            Status[ARRAYSIZE(Status) - 1] = UNICODE_NULL;
            RperfSetStatus(Status);
            return 0;
        }

        case WM_RPERF_CAPTURE_DONE:
        {
            DWORD Error = (DWORD)WParam;
            if (App.Recorder != NULL)
            {
                RperfFinishRecorderCapture();
                if (Error == ERROR_SUCCESS)
                {
                    if (!RperfOpenLogPath(App.RecorderLogPath))
                        RperfSetCapturing(FALSE);
                }
                else
                {
                    RperfSetCapturing(FALSE);
                    RperfClearViews(L"Recording ended without a usable sample.");
                    RperfShowSystemError(Window, L"Recording the profile", Error);
                }
                return 0;
            }
            RperfCaptureWait(&App.Session);
            if (App.Session.SampleCount != 0)
                RperfShowSession();
            else
                RperfClearViews(L"Recording ended without a usable stack sample.");
            RperfSetCapturing(FALSE);
            if (Error == ERROR_NOT_SUPPORTED && App.Session.CrossBitnessTargetBits != 0)
            {
                WCHAR Bitness[192];
                _snwprintf(Bitness, ARRAYSIZE(Bitness), L"This %u-bit profiler cannot walk the stacks of the %lu-bit target process. Use the profiler build that matches the target's bitness.", (unsigned)(sizeof(PVOID) * 8), App.Session.CrossBitnessTargetBits);
                Bitness[ARRAYSIZE(Bitness) - 1] = UNICODE_NULL;
                MessageBoxW(Window, Bitness, App.Title, MB_OK | MB_ICONWARNING);
            }
            else if (Error != ERROR_SUCCESS)
                RperfShowSystemError(Window, L"Recording the profile", Error);
            return 0;
        }

        case WM_RPERF_JOB_PROGRESS:
        {
            RPERF_JOB_KIND Kind = (RPERF_JOB_KIND)WParam;
            ULONG Percent = (ULONG)LParam;
            WCHAR Status[160];

            if (!App.Processing || Kind != App.JobKind)
                return 0;
            _snwprintf(Status, ARRAYSIZE(Status),
                       Kind == RperfJobOpen ?
                           L"Opening and validating the profile log... %lu%%" :
                       Kind == RperfJobSymbolize ?
                           L"Resolving local symbols... %lu%%" :
                           L"Building profile views... %lu%%",
                       min(Percent, 100));
            Status[ARRAYSIZE(Status) - 1] = UNICODE_NULL;
            RperfSetStatus(Status);
            return 0;
        }

        case WM_RPERF_JOB_COMPLETE:
        {
            RPERF_JOB_KIND Kind = (RPERF_JOB_KIND)WParam;
            DWORD Status = (DWORD)LParam;
            BOOL Committed;
            DWORD Error;

            if (!App.Processing || Kind != App.JobKind)
                return 0;
            Committed = RperfControllerCommitCompleted(&App.Controller,
                                                       App.JobGeneration);
            Error = Committed ? ERROR_SUCCESS : GetLastError();
            if (Error == ERROR_SUCCESS && Status != ERROR_SUCCESS)
                Error = Status;
            if (Kind == RperfJobSymbolize && App.JobSymbolProvider != NULL)
            {
                if (RperfQuerySymbolProviderSummary(App.JobSymbolProvider,
                                                    &App.JobSymbolSummary))
                    App.JobHasSymbolSummary = TRUE;
                RperfDestroySymbolProvider(App.JobSymbolProvider);
                App.JobSymbolProvider = NULL;
            }
            if (!Committed)
            {
                if (Kind == RperfJobSymbolize &&
                    Error != ERROR_CANCELLED)
                {
                    RperfBeginControllerPresentation(Error);
                    return 0;
                }
                RperfSetProcessing(FALSE);
                if (Error == ERROR_CANCELLED)
                    RperfSetStatus(L"Opening the profile log was canceled; "
                                   L"the previous profile is unchanged.");
                else
                {
                    RperfSetStatus(Kind == RperfJobPrepareLegacy ?
                                   L"The profile views could not be built." :
                                   L"The profile log could not be opened.");
                    RperfShowSystemError(App.MainWindow,
                                         Kind == RperfJobPrepareLegacy ?
                                             L"Preparing the profile views" :
                                             L"Opening the profile log",
                                         Error);
                }
                return 0;
            }
            if (Kind == RperfJobOpen)
            {
                RperfBeginLoadedLogSymbolization();
                return 0;
            }
            if (Kind == RperfJobSymbolize)
            {
                RperfBeginControllerPresentation(ERROR_SUCCESS);
                return 0;
            }
            RperfCompleteControllerPresentation();
            return 0;
        }

        case WM_RPERF_FLAME_HOVER:
        {
            ULONG NodeIndex = (ULONG)WParam;
            if (NodeIndex == RPERF_INVALID_NODE ||
                NodeIndex >= App.Session.NodeCount)
            {
                RperfSetSessionStatus();
            }
            else
            {
                const RPERF_NODE *Node = &App.Session.Nodes[NodeIndex];
                WCHAR Symbol[384], Status[512];
                RperfFormatSymbol(&App.Session,
                                  Node->Address,
                                  Symbol,
                                  ARRAYSIZE(Symbol));
                _snwprintf(Status,
                           ARRAYSIZE(Status),
                           L"%s: %I64u samples (%.2f%% of current view).",
                           Symbol,
                           Node->Count,
                           100.0 * (double)Node->Count /
                           (double)App.Session.FilteredSampleCount);
                Status[ARRAYSIZE(Status) - 1] = UNICODE_NULL;
                RperfSetStatus(Status);
            }
            return 0;
        }

        case WM_RPERF_TIMELINE_RANGE_CHANGED:
        {
            const RPERF_TIME_RANGE *Range =
                (const RPERF_TIME_RANGE *)LParam;
            LRESULT Selection, Data;
            DWORD ThreadId = 0;

            if (Range == NULL || App.Session.SampleCount == 0 ||
                RperfIsBusy())
                return 0;
            Selection = SendMessageW(App.ThreadCombo,
                                     CB_GETCURSEL,
                                     0,
                                     0);
            Data = Selection != CB_ERR ?
                   SendMessageW(App.ThreadCombo,
                                CB_GETITEMDATA,
                                Selection,
                                0) : 0;
            if (Data != CB_ERR)
                ThreadId = (DWORD)(ULONG_PTR)Data;
            RperfApplyFilterValues(ThreadId, Range->StartUs, Range->EndUs, RperfFilterFlagsFromControls());
            return 0;
        }

        case WM_CLOSE:
            if (App.Processing)
            {
                if (MessageBoxW(Window,
                                L"A profile is being opened or symbolized. "
                                L"Cancel it and exit?",
                                App.Title,
                                MB_YESNO | MB_ICONQUESTION) != IDYES)
                {
                    return 0;
                }
                RperfSetStatus(L"Canceling offline analysis before exit...");
                RperfControllerCancel(&App.Controller);
            }
            if (RperfIsCapturing())
            {
                if (MessageBoxW(Window,
                                L"A recording is active. Stop it and exit?",
                                App.Title,
                                MB_YESNO | MB_ICONQUESTION) != IDYES)
                {
                    return 0;
                }
                RperfSetStatus(L"Stopping the recorder safely before exit...");
                if (App.Recorder != NULL)
                {
                    RperfRecorderRequestStop(App.Recorder);
                    RperfFinishRecorderCapture();
                }
                else
                {
                    RperfCaptureStop(&App.Session);
                    RperfCaptureWait(&App.Session);
                }
            }
            DestroyWindow(Window);
            return 0;

        case WM_DESTROY:
            if (App.Recorder != NULL)
            {
                RperfRecorderRequestStop(App.Recorder);
                RperfFinishRecorderCapture();
            }
            if (App.ControllerInitialized)
            {
                RperfControllerDestroy(&App.Controller);
                App.ControllerInitialized = FALSE;
            }
            if (App.JobSymbolProvider != NULL)
            {
                RperfDestroySymbolProvider(App.JobSymbolProvider);
                App.JobSymbolProvider = NULL;
            }
            RperfSessionClear(&App.Session);
            if (App.Processes != NULL)
            {
                RperfFreeProcesses(App.Processes);
                App.Processes = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

int WINAPI
wWinMain(HINSTANCE Instance,
         HINSTANCE PreviousInstance,
         PWSTR CommandLine,
         INT ShowCommand)
{
    INITCOMMONCONTROLSEX Controls;
    WNDCLASSEXW Class;
    HWND Window;
    MSG Message;
    HACCEL Accelerators;
    WCHAR StartupLog[MAX_PATH];
    ACCEL AcceleratorTable[] =
    {
        { FCONTROL | FVIRTKEY, 'O', IDM_FILE_OPEN },
        { FCONTROL | FVIRTKEY, 'R', IDM_CAPTURE_RECORD },
        { FCONTROL | FVIRTKEY, 'L', IDM_CAPTURE_LAUNCH },
        { FVIRTKEY, VK_F5, IDM_CAPTURE_REFRESH }
    };

    UNREFERENCED_PARAMETER(PreviousInstance);

    ZeroMemory(&App, sizeof(App));
    App.Instance = Instance;
    App.SortColumn = 3;
    App.SortAscending = FALSE;
    RperfSessionInitialize(&App.Session);
    if (!LoadStringW(Instance, IDS_APP_TITLE, App.Title, ARRAYSIZE(App.Title)))
        lstrcpyW(App.Title, L"ReactOS Performance Analyzer");

    ZeroMemory(&Controls, sizeof(Controls));
    Controls.dwSize = sizeof(Controls);
    Controls.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES |
                     ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&Controls);

    if (!RperfRegisterFlameGraph(Instance))
        return 1;
    if (!RperfRegisterTimeline(Instance))
    {
        RperfUnregisterFlameGraph(Instance);
        return 1;
    }

    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.style = CS_HREDRAW | CS_VREDRAW;
    Class.lpfnWndProc = RperfMainWndProc;
    Class.hInstance = Instance;
    Class.hIcon = LoadIconW(Instance, MAKEINTRESOURCEW(IDI_ROSPROFILER));
    if (Class.hIcon == NULL)
        Class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    Class.hIconSm = Class.hIcon;
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    Class.lpszClassName = RPERF_MAIN_CLASS;
    if (!RegisterClassExW(&Class))
    {
        RperfUnregisterTimeline(Instance);
        RperfUnregisterFlameGraph(Instance);
        return 1;
    }

    RperfControllerInitialize(&App.Controller);
    App.ControllerInitialized = TRUE;
    Window = CreateWindowExW(0,
                             RPERF_MAIN_CLASS,
                             App.Title,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             1020,
                             700,
                             NULL,
                             RperfCreateMainMenu(),
                             Instance,
                             NULL);
    if (Window == NULL)
    {
        if (App.ControllerInitialized)
        {
            RperfControllerDestroy(&App.Controller);
            App.ControllerInitialized = FALSE;
        }
        UnregisterClassW(RPERF_MAIN_CLASS, Instance);
        RperfUnregisterTimeline(Instance);
        RperfUnregisterFlameGraph(Instance);
        return 1;
    }

    Accelerators = CreateAcceleratorTableW(AcceleratorTable,
                                            ARRAYSIZE(AcceleratorTable));
    ShowWindow(Window, ShowCommand);
    UpdateWindow(Window);
    if (RperfGetStartupLogPath(CommandLine,
                               StartupLog,
                               ARRAYSIZE(StartupLog)))
    {
        RperfOpenLogPath(StartupLog);
    }

    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        if (Accelerators != NULL &&
            TranslateAcceleratorW(Window, Accelerators, &Message))
            continue;
        if (IsDialogMessageW(Window, &Message))
            continue;
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }

    if (Accelerators != NULL)
        DestroyAcceleratorTable(Accelerators);
    UnregisterClassW(RPERF_MAIN_CLASS, Instance);
    RperfUnregisterTimeline(Instance);
    RperfUnregisterFlameGraph(Instance);
    return (INT)Message.wParam;
}
