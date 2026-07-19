/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Interactive thread timeline control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rosprofiler.h"
#include "profiler_viewmodel.h"

#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>

#define RPERF_TIMELINE_HEADER_HEIGHT 70
#define RPERF_TIMELINE_LABEL_WIDTH 88
#define RPERF_TIMELINE_RIGHT_MARGIN 12
#define RPERF_TIMELINE_LANE_HEIGHT 24
#define RPERF_TIMELINE_MAX_LANES RPERF_TIMELINE_DEFAULT_MAX_LANES
#define RPERF_TIMELINE_TICK_SPACING 120
#define RPERF_TIMELINE_MIN_BUCKETS 256
#define RPERF_TIMELINE_MAX_BUCKETS 2048

#define RPERF_TIMELINE_DIAGNOSTIC_FLAGS \
    (RPERF_SAMPLE_TRUNCATED | RPERF_SAMPLE_UNWIND_FAILED)

typedef struct _RPERF_TIMELINE_BUCKET
{
    USHORT Running;
    USHORT Waiting;
    USHORT Unknown;
    USHORT Diagnostic;
    USHORT OutsideFilter;
    USHORT WaitingExcluded;
} RPERF_TIMELINE_BUCKET;

typedef struct _RPERF_TIMELINE_COUNTS
{
    SIZE_T Selected;
    SIZE_T Running;
    SIZE_T Waiting;
    SIZE_T Unknown;
    SIZE_T Diagnostic;
    SIZE_T WaitingExcluded;
} RPERF_TIMELINE_COUNTS;

typedef struct _RPERF_TIMELINE_STATE
{
    const RPERF_SESSION *Session;
    RPERF_TIMELINE_VIEW *View;
    RPERF_TIME_RANGE Range;
    RPERF_TIMELINE_COUNTS Counts;
    DWORD ThreadIds[RPERF_TIMELINE_MAX_LANES];
    SIZE_T ThreadCount;
    ULONG LaneCount;
    BOOL ThreadCountExact;
    BOOL CountsValid;
    BOOL Dragging;
    BOOL TrackingMouse;
    ULONG TrackMode;
    SIZE_T HoverLane;
    SIZE_T HoverBucket;
    ULONGLONG DragAnchorUs;
    RPERF_TIME_RANGE DragOriginalRange;
} RPERF_TIMELINE_STATE;

static BOOL
RperfTimelineSampleInRange(const RPERF_TIMELINE_STATE *State,
                           const RPERF_SAMPLE *Sample)
{
    return Sample->TimeUs >= State->Range.StartUs &&
           Sample->TimeUs <= State->Range.EndUs;
}

static BOOL
RperfTimelineSampleMatchesThread(const RPERF_TIMELINE_STATE *State,
                                 const RPERF_SAMPLE *Sample)
{
    return State->Session->FilterThreadId == 0 ||
           State->Session->FilterThreadId == Sample->ThreadId;
}

static BOOL
RperfTimelineSampleWaiting(const RPERF_SAMPLE *Sample)
{
    return (Sample->Flags & (RPERF_SAMPLE_STATE_KNOWN |
                             RPERF_SAMPLE_WAITING)) ==
           (RPERF_SAMPLE_STATE_KNOWN | RPERF_SAMPLE_WAITING);
}

static BOOL
RperfTimelineSampleSelected(const RPERF_TIMELINE_STATE *State,
                            const RPERF_SAMPLE *Sample)
{
    if (!RperfTimelineSampleInRange(State, Sample) ||
        !RperfTimelineSampleMatchesThread(State, Sample))
    {
        return FALSE;
    }
    return (State->Session->FilterFlags & RPERF_FILTER_CPU_ONLY) == 0 ||
           !RperfTimelineSampleWaiting(Sample);
}

static VOID
RperfTimelineCountSamples(RPERF_TIMELINE_STATE *State,
                          RPERF_TIMELINE_COUNTS *Counts)
{
    const RPERF_SESSION *Session = State->Session;
    RPERF_TIMELINE_COUNTS *Cached = &State->Counts;
    SIZE_T Index;

    if (State->CountsValid)
    {
        *Counts = *Cached;
        return;
    }
    ZeroMemory(Cached, sizeof(*Cached));
    if (Session == NULL || Session->Samples == NULL)
    {
        State->CountsValid = TRUE;
        *Counts = *Cached;
        return;
    }

    for (Index = 0; Index < Session->SampleCount; Index++)
    {
        const RPERF_SAMPLE *Sample = &Session->Samples[Index];

        if (!RperfTimelineSampleSelected(State, Sample))
        {
            if (RperfTimelineSampleInRange(State, Sample) &&
                RperfTimelineSampleMatchesThread(State, Sample) &&
                RperfTimelineSampleWaiting(Sample))
            {
                Cached->WaitingExcluded++;
            }
            continue;
        }

        Cached->Selected++;
        if (Sample->Flags & RPERF_TIMELINE_DIAGNOSTIC_FLAGS)
            Cached->Diagnostic++;
        if ((Sample->Flags & RPERF_SAMPLE_STATE_KNOWN) == 0)
            Cached->Unknown++;
        else if (RperfTimelineSampleWaiting(Sample))
            Cached->Waiting++;
        else
            Cached->Running++;
    }
    State->CountsValid = TRUE;
    *Counts = *Cached;
}

static PCWSTR
RperfTimelineModeText(const RPERF_SESSION *Session)
{
    if (Session->Backend == RperfBackendKernel)
        return L"RosProf on-CPU";
    if (Session->Backend == RperfBackendEtw)
        return L"ETW on-CPU";
    if (Session->Backend == RperfBackendFake)
        return L"Synthetic on-CPU";
    if (Session->FilterFlags & RPERF_FILTER_CPU_ONLY)
        return L"Classic CPU-only";
    return L"Classic wall clock";
}

static LONG
RperfTimelineDrawLegendItem(HDC Dc,
                            LONG X,
                            LONG Y,
                            COLORREF Color,
                            PCWSTR Label)
{
    RECT Swatch, TextRect;
    HBRUSH Brush;
    SIZE Size;

    Swatch.left = X;
    Swatch.top = Y + 3;
    Swatch.right = X + 10;
    Swatch.bottom = Y + 13;
    Brush = CreateSolidBrush(Color);
    if (Brush != NULL)
    {
        FillRect(Dc, &Swatch, Brush);
        DeleteObject(Brush);
    }
    FrameRect(Dc, &Swatch, GetSysColorBrush(COLOR_3DSHADOW));

    GetTextExtentPoint32W(Dc, Label, lstrlenW(Label), &Size);
    TextRect.left = X + 14;
    TextRect.top = Y;
    TextRect.right = TextRect.left + Size.cx + 3;
    TextRect.bottom = Y + 18;
    DrawTextW(Dc, Label, -1, &TextRect,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    return TextRect.right + 12;
}

static int __cdecl
RperfTimelineCompareThreadIds(const void *Left,
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
RperfTimelineBuildLanes(RPERF_TIMELINE_STATE *State)
{
    const RPERF_SESSION *Session = State->Session;
    DWORD *ThreadIds = NULL;
    SIZE_T Index;

    State->ThreadCount = 0;
    State->LaneCount = 0;
    State->ThreadCountExact = TRUE;

    if (Session == NULL || Session->Samples == NULL ||
        Session->SampleCount == 0)
    {
        return;
    }

    if (Session->SampleCount <= ((SIZE_T)-1) / sizeof(*ThreadIds))
    {
        ThreadIds = HeapAlloc(GetProcessHeap(),
                              0,
                              Session->SampleCount * sizeof(*ThreadIds));
    }

    if (ThreadIds != NULL)
    {
        for (Index = 0; Index < Session->SampleCount; Index++)
            ThreadIds[Index] = Session->Samples[Index].ThreadId;

        qsort(ThreadIds,
              Session->SampleCount,
              sizeof(*ThreadIds),
              RperfTimelineCompareThreadIds);

        for (Index = 0; Index < Session->SampleCount; Index++)
        {
            if (Index != 0 && ThreadIds[Index] == ThreadIds[Index - 1])
                continue;

            if (State->LaneCount < RPERF_TIMELINE_MAX_LANES)
                State->ThreadIds[State->LaneCount++] = ThreadIds[Index];
            State->ThreadCount++;
        }

        HeapFree(GetProcessHeap(), 0, ThreadIds);
        return;
    }

    /* Retain a useful, bounded view if the exact-count scratch buffer fails. */
    for (Index = 0; Index < Session->SampleCount; Index++)
    {
        DWORD ThreadId = Session->Samples[Index].ThreadId;
        ULONG Lane;

        for (Lane = 0; Lane < State->LaneCount; Lane++)
        {
            if (State->ThreadIds[Lane] == ThreadId)
                break;
        }

        if (Lane != State->LaneCount)
            continue;

        if (State->LaneCount < RPERF_TIMELINE_MAX_LANES)
        {
            State->ThreadIds[State->LaneCount++] = ThreadId;
            State->ThreadCount++;
        }
        else
        {
            State->ThreadCountExact = FALSE;
        }
    }

    qsort(State->ThreadIds,
          State->LaneCount,
          sizeof(State->ThreadIds[0]),
          RperfTimelineCompareThreadIds);

    if (!State->ThreadCountExact)
        State->ThreadCount = State->LaneCount + 1;
}

static VOID
RperfTimelineSetRange(RPERF_TIMELINE_STATE *State,
                      const RPERF_TIME_RANGE *Range)
{
    ULONGLONG Duration = State->Session != NULL ?
                         State->Session->ElapsedUs : 0;
    ULONGLONG Start = Range != NULL ? Range->StartUs : 0;
    ULONGLONG End = Range != NULL ? Range->EndUs : 0;

    if (Start > Duration)
        Start = Duration;
    if (End > Duration)
        End = Duration;
    if (Start > End)
    {
        ULONGLONG Temporary = Start;
        Start = End;
        End = Temporary;
    }

    State->Range.StartUs = Start;
    State->Range.EndUs = End;
    State->CountsValid = FALSE;
}

static BOOL
RperfTimelineGetGraphRect(HWND Window,
                          RECT *GraphRect)
{
    RECT Client;

    GetClientRect(Window, &Client);
    GraphRect->left = RPERF_TIMELINE_LABEL_WIDTH;
    GraphRect->top = RPERF_TIMELINE_HEADER_HEIGHT;
    GraphRect->right = Client.right - RPERF_TIMELINE_RIGHT_MARGIN;
    GraphRect->bottom = Client.bottom;
    return GraphRect->right > GraphRect->left &&
           GraphRect->bottom > GraphRect->top;
}

static VOID
RperfTimelineGetTrackButtonRectsFromClient(const RECT *Client,
                                           RECT *ThreadRect,
                                           RECT *CpuRect)
{
    CpuRect->right = Client->right - 8;
    CpuRect->left = CpuRect->right - 48;
    CpuRect->top = 3;
    CpuRect->bottom = 22;
    ThreadRect->right = CpuRect->left - 4;
    ThreadRect->left = ThreadRect->right - 64;
    ThreadRect->top = CpuRect->top;
    ThreadRect->bottom = CpuRect->bottom;
}

static VOID
RperfTimelineGetTrackButtonRects(HWND Window,
                                 RECT *ThreadRect,
                                 RECT *CpuRect)
{
    RECT Client;

    GetClientRect(Window, &Client);
    RperfTimelineGetTrackButtonRectsFromClient(&Client,
                                               ThreadRect,
                                               CpuRect);
}

static SIZE_T
RperfTimelineBucketCount(HWND Window)
{
    RECT GraphRect;
    SIZE_T BucketCount;
    LONG Width;

    Width = RperfTimelineGetGraphRect(Window, &GraphRect) ?
            GraphRect.right - GraphRect.left :
            RPERF_TIMELINE_MIN_BUCKETS;
    BucketCount = Width > 0 ? (SIZE_T)Width : RPERF_TIMELINE_MIN_BUCKETS;
    if (BucketCount < RPERF_TIMELINE_MIN_BUCKETS)
        BucketCount = RPERF_TIMELINE_MIN_BUCKETS;
    if (BucketCount > RPERF_TIMELINE_MAX_BUCKETS)
        BucketCount = RPERF_TIMELINE_MAX_BUCKETS;
    return BucketCount;
}

static VOID
RperfTimelineSetView(RPERF_TIMELINE_STATE *State,
                     RPERF_TIMELINE_VIEW *View)
{
    if (State->View != NULL)
        RperfTimelineViewDestroy(State->View);
    State->View = View;
    if (State->TrackMode == RPERF_TIMELINE_TRACK_CPUS &&
        (View == NULL || View->CpuCount == 0))
    {
        State->TrackMode = RPERF_TIMELINE_TRACK_THREADS;
    }
}

static LONG
RperfTimelineTimeToOffset(ULONGLONG TimeUs,
                          ULONGLONG DurationUs,
                          LONG Width)
{
    double Position;

    if (Width <= 0 || DurationUs == 0)
        return 0;
    if (TimeUs >= DurationUs)
        return Width;

    Position = ((double)TimeUs * (double)Width) / (double)DurationUs;
    if (Position <= 0.0)
        return 0;
    if (Position >= (double)Width)
        return Width;
    return (LONG)Position;
}

static ULONGLONG
RperfTimelineOffsetToTime(LONG Offset,
                          LONG Width,
                          ULONGLONG DurationUs)
{
    ULONGLONG Quotient, Remainder;

    if (Width <= 0 || Offset <= 0 || DurationUs == 0)
        return 0;
    if (Offset >= Width)
        return DurationUs;

    Quotient = DurationUs / (ULONGLONG)Width;
    Remainder = DurationUs % (ULONGLONG)Width;
    return Quotient * (ULONGLONG)Offset +
           (Remainder * (ULONGLONG)Offset) / (ULONGLONG)Width;
}

static ULONGLONG
RperfTimelinePointToTime(const RPERF_TIMELINE_STATE *State,
                         const RECT *GraphRect,
                         LONG X)
{
    LONG Width = GraphRect->right - GraphRect->left;
    LONG Offset = X - GraphRect->left;

    if (Offset < 0)
        Offset = 0;
    if (Offset > Width)
        Offset = Width;
    return RperfTimelineOffsetToTime(Offset,
                                     Width,
                                     State->Session->ElapsedUs);
}

static VOID
RperfTimelineFormatTime(ULONGLONG TimeUs,
                        PWSTR Buffer,
                        SIZE_T BufferCount)
{
    ULONGLONG Milliseconds, Seconds, Minutes, Hours;

    if (TimeUs < 1000)
    {
        _snwprintf(Buffer, BufferCount, L"%I64u us", TimeUs);
    }
    else if (TimeUs < 1000000)
    {
        Milliseconds = TimeUs / 1000;
        if (TimeUs % 1000 >= 100)
        {
            _snwprintf(Buffer,
                       BufferCount,
                       L"%I64u.%I64u ms",
                       Milliseconds,
                       (TimeUs % 1000) / 100);
        }
        else
        {
            _snwprintf(Buffer, BufferCount, L"%I64u ms", Milliseconds);
        }
    }
    else if (TimeUs < 60000000)
    {
        Seconds = TimeUs / 1000000;
        _snwprintf(Buffer,
                   BufferCount,
                   L"%I64u.%02I64u s",
                   Seconds,
                   (TimeUs % 1000000) / 10000);
    }
    else
    {
        Seconds = TimeUs / 1000000;
        Minutes = Seconds / 60;
        Hours = Minutes / 60;
        if (Hours != 0)
        {
            _snwprintf(Buffer,
                       BufferCount,
                       L"%I64u:%02I64u:%02I64u",
                       Hours,
                       Minutes % 60,
                       Seconds % 60);
        }
        else
        {
            _snwprintf(Buffer,
                       BufferCount,
                       L"%I64u:%02I64u",
                       Minutes,
                       Seconds % 60);
        }
    }

    Buffer[BufferCount - 1] = UNICODE_NULL;
}

static ULONG
RperfTimelineFindLane(const RPERF_TIMELINE_STATE *State,
                      DWORD ThreadId,
                      ULONG VisibleLanes)
{
    ULONG Low = 0;
    ULONG High = VisibleLanes;

    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;

        if (State->ThreadIds[Middle] < ThreadId)
            Low = Middle + 1;
        else
            High = Middle;
    }

    if (Low < VisibleLanes && State->ThreadIds[Low] == ThreadId)
        return Low;
    return RPERF_TIMELINE_MAX_LANES;
}

static VOID
RperfTimelineIncrement(USHORT *Value)
{
    if (*Value != 0xffff)
        (*Value)++;
}

static ULONG
RperfTimelineSamplePen(const RPERF_TIMELINE_STATE *State,
                       const RPERF_SAMPLE *Sample)
{
    if (!RperfTimelineSampleSelected(State, Sample))
    {
        if (RperfTimelineSampleInRange(State, Sample) &&
            RperfTimelineSampleMatchesThread(State, Sample) &&
            RperfTimelineSampleWaiting(Sample))
        {
            return 7;
        }
        return 6;
    }
    if (Sample->Flags & RPERF_TIMELINE_DIAGNOSTIC_FLAGS)
        return 5;
    if ((Sample->Flags & RPERF_SAMPLE_STATE_KNOWN) == 0)
        return 4;
    if (RperfTimelineSampleWaiting(Sample))
        return 3;
    return 0;
}

static VOID
RperfTimelineDrawMarkers(HDC Dc,
                         const RECT *GraphRect,
                         const RPERF_TIMELINE_STATE *State,
                         ULONG VisibleLanes)
{
    const RPERF_SESSION *Session = State->Session;
    LONG Width = GraphRect->right - GraphRect->left;
    SIZE_T Columns = (SIZE_T)Width + 1;
    SIZE_T BucketCount;
    RPERF_TIMELINE_BUCKET *Buckets = NULL;
    HPEN Pens[8];
    HPEN OldPen, CurrentPen;
    SIZE_T SampleIndex;
    ULONG Lane;

    if (Session == NULL || Session->Samples == NULL ||
        Session->SampleCount == 0 || VisibleLanes == 0 || Width <= 0)
    {
        return;
    }

    if (Columns <= ((SIZE_T)-1) / VisibleLanes &&
        Columns * VisibleLanes <= ((SIZE_T)-1) / sizeof(*Buckets))
    {
        BucketCount = Columns * VisibleLanes;
        Buckets = HeapAlloc(GetProcessHeap(),
                            HEAP_ZERO_MEMORY,
                            BucketCount * sizeof(*Buckets));
    }

    Pens[0] = CreatePen(PS_SOLID, 1, RGB(102, 157, 207));
    Pens[1] = CreatePen(PS_SOLID, 1, RGB(57, 119, 180));
    Pens[2] = CreatePen(PS_SOLID, 1, RGB(21, 82, 143));
    Pens[3] = CreatePen(PS_SOLID, 1, RGB(225, 146, 45));
    Pens[4] = CreatePen(PS_SOLID, 1, RGB(99, 118, 135));
    Pens[5] = CreatePen(PS_SOLID, 1, RGB(190, 55, 45));
    Pens[6] = CreatePen(PS_SOLID, 1, RGB(194, 202, 211));
    Pens[7] = CreatePen(PS_SOLID, 1, RGB(226, 197, 155));
    CurrentPen = Pens[0] != NULL ? Pens[0] :
                 (HPEN)GetStockObject(BLACK_PEN);
    OldPen = SelectObject(Dc, CurrentPen);

    if (Buckets != NULL)
    {
        for (SampleIndex = 0; SampleIndex < Session->SampleCount; SampleIndex++)
        {
            const RPERF_SAMPLE *Sample = &Session->Samples[SampleIndex];
            LONG Offset;
            SIZE_T BucketIndex;
            RPERF_TIMELINE_BUCKET *Bucket;

            Lane = RperfTimelineFindLane(State,
                                         Sample->ThreadId,
                                         VisibleLanes);
            if (Lane == RPERF_TIMELINE_MAX_LANES)
                continue;

            Offset = RperfTimelineTimeToOffset(Sample->TimeUs,
                                               Session->ElapsedUs,
                                               Width);
            BucketIndex = (SIZE_T)Lane * Columns + (SIZE_T)Offset;
            Bucket = &Buckets[BucketIndex];
            switch (RperfTimelineSamplePen(State, Sample))
            {
                case 0: RperfTimelineIncrement(&Bucket->Running); break;
                case 3: RperfTimelineIncrement(&Bucket->Waiting); break;
                case 4: RperfTimelineIncrement(&Bucket->Unknown); break;
                case 5: RperfTimelineIncrement(&Bucket->Diagnostic); break;
                case 7: RperfTimelineIncrement(&Bucket->WaitingExcluded); break;
                default: RperfTimelineIncrement(&Bucket->OutsideFilter); break;
            }
        }

        for (Lane = 0; Lane < VisibleLanes; Lane++)
        {
            LONG LaneTop = GraphRect->top +
                           (LONG)Lane * RPERF_TIMELINE_LANE_HEIGHT;
            SIZE_T Column;

            for (Column = 0; Column < Columns; Column++)
            {
                const RPERF_TIMELINE_BUCKET *Bucket =
                    &Buckets[(SIZE_T)Lane * Columns + Column];
                HPEN Pen;
                ULONG Count;

                if (Bucket->Diagnostic != 0)
                {
                    Pen = Pens[5];
                }
                else if (Bucket->Running != 0 || Bucket->Waiting != 0 ||
                         Bucket->Unknown != 0)
                {
                    if (Bucket->Waiting > Bucket->Running &&
                        Bucket->Waiting >= Bucket->Unknown)
                    {
                        Pen = Pens[3];
                    }
                    else if (Bucket->Unknown > Bucket->Running)
                    {
                        Pen = Pens[4];
                    }
                    else
                    {
                        Count = Bucket->Running + Bucket->Waiting +
                                Bucket->Unknown;
                        Pen = Count >= 8 ? Pens[2] :
                              Count >= 3 ? Pens[1] : Pens[0];
                    }
                }
                else if (Bucket->WaitingExcluded != 0)
                {
                    Pen = Pens[7];
                }
                else if (Bucket->OutsideFilter != 0)
                {
                    Pen = Pens[6];
                }
                else
                {
                    continue;
                }
                if (Pen == NULL)
                    Pen = (HPEN)GetStockObject(BLACK_PEN);
                if (Pen != CurrentPen)
                {
                    SelectObject(Dc, Pen);
                    CurrentPen = Pen;
                }

                MoveToEx(Dc,
                         GraphRect->left + (LONG)Column,
                         LaneTop + 4,
                         NULL);
                LineTo(Dc,
                       GraphRect->left + (LONG)Column,
                       LaneTop + RPERF_TIMELINE_LANE_HEIGHT - 4);
            }
        }

        HeapFree(GetProcessHeap(), 0, Buckets);
    }
    else
    {
        for (SampleIndex = 0; SampleIndex < Session->SampleCount; SampleIndex++)
        {
            const RPERF_SAMPLE *Sample = &Session->Samples[SampleIndex];
            HPEN Pen = Pens[RperfTimelineSamplePen(State, Sample)];
            LONG X, LaneTop;

            Lane = RperfTimelineFindLane(State,
                                         Sample->ThreadId,
                                         VisibleLanes);
            if (Lane == RPERF_TIMELINE_MAX_LANES)
                continue;
            if (Pen == NULL)
                Pen = (HPEN)GetStockObject(BLACK_PEN);
            if (Pen != CurrentPen)
            {
                SelectObject(Dc, Pen);
                CurrentPen = Pen;
            }

            X = GraphRect->left +
                RperfTimelineTimeToOffset(Sample->TimeUs,
                                          Session->ElapsedUs,
                                          Width);
            LaneTop = GraphRect->top +
                      (LONG)Lane * RPERF_TIMELINE_LANE_HEIGHT;
            MoveToEx(Dc, X, LaneTop + 4, NULL);
            LineTo(Dc,
                   X,
                   LaneTop + RPERF_TIMELINE_LANE_HEIGHT - 4);
        }
    }

    SelectObject(Dc, OldPen);
    for (Lane = 0; Lane < ARRAYSIZE(Pens); Lane++)
    {
        if (Pens[Lane] != NULL)
            DeleteObject(Pens[Lane]);
    }
}

static LONG
RperfTimelineBucketToX(const RPERF_TIMELINE_VIEW *View,
                       const RECT *GraphRect,
                       SIZE_T Bucket)
{
    LONG Width = GraphRect->right - GraphRect->left;

    if (View == NULL || View->BucketCount == 0)
        return GraphRect->left;
    return GraphRect->left +
           RperfTimelineTimeToOffset(Bucket,
                                     View->BucketCount,
                                     Width);
}

static BOOL
RperfTimelineModelCellSelected(const RPERF_TIMELINE_STATE *State,
                               const RPERF_TIMELINE_VIEW *View,
                               SIZE_T Lane,
                               SIZE_T Bucket,
                               BOOL CpuMode)
{
    ULONGLONG StartUs, EndUs;

    StartUs = (Bucket * View->BucketWidthNs) / 1000;
    EndUs = ((Bucket + 1) * View->BucketWidthNs) / 1000;
    if (EndUs < State->Range.StartUs || StartUs > State->Range.EndUs)
        return FALSE;
    if (!CpuMode && State->Session->FilterThreadId != 0 &&
        View->Lanes[Lane].ThreadId != State->Session->FilterThreadId)
    {
        return FALSE;
    }
    return TRUE;
}

static VOID
RperfTimelineDrawTrackButton(HDC Dc,
                             const RECT *Rect,
                             PCWSTR Text,
                             BOOL Selected,
                             BOOL Enabled)
{
    HBRUSH Brush;
    COLORREF OldColor;

    Brush = CreateSolidBrush(Selected ? RGB(213, 232, 250) :
                             RGB(242, 244, 247));
    if (Brush != NULL)
    {
        FillRect(Dc, Rect, Brush);
        DeleteObject(Brush);
    }
    FrameRect(Dc, Rect, GetSysColorBrush(Selected ?
                                         COLOR_HIGHLIGHT :
                                         COLOR_3DSHADOW));
    OldColor = SetTextColor(Dc, Enabled ?
                            GetSysColor(COLOR_WINDOWTEXT) :
                            GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(Dc,
              Text,
              -1,
              (LPRECT)Rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(Dc, OldColor);
}

static VOID
RperfTimelineDrawModelMarkers(HDC Dc,
                              const RECT *GraphRect,
                              const RPERF_TIMELINE_STATE *State,
                              ULONG VisibleLanes,
                              BOOL CpuMode)
{
    const RPERF_TIMELINE_VIEW *View = State->View;
    HBRUSH RunningBrush, ReadyBrush, WaitingBrush, OutsideBrush;
    HBRUSH WakeupBrush, StartBrush;
    HPEN SamplePen, OutsidePen, EventPen, EndPen, OldPen;
    ULONG Lane;

    if (View == NULL || View->BucketCount == 0 || VisibleLanes == 0)
        return;
    RunningBrush = CreateSolidBrush(RGB(57, 119, 180));
    ReadyBrush = CreateSolidBrush(RGB(237, 177, 32));
    WaitingBrush = CreateSolidBrush(RGB(151, 103, 190));
    OutsideBrush = CreateSolidBrush(RGB(199, 207, 216));
    WakeupBrush = CreateSolidBrush(RGB(56, 142, 60));
    StartBrush = CreateSolidBrush(RGB(43, 152, 92));
    SamplePen = CreatePen(PS_SOLID, 1, RGB(35, 92, 150));
    OutsidePen = CreatePen(PS_SOLID, 1, RGB(199, 207, 216));
    EventPen = CreatePen(PS_SOLID, 1, RGB(52, 59, 66));
    EndPen = CreatePen(PS_SOLID, 1, RGB(138, 43, 43));
    OldPen = SelectObject(Dc,
                          SamplePen != NULL ? SamplePen :
                          (HPEN)GetStockObject(BLACK_PEN));

    for (Lane = 0; Lane < VisibleLanes; Lane++)
    {
        SIZE_T Bucket;
        LONG LaneTop = GraphRect->top +
                       (LONG)Lane * RPERF_TIMELINE_LANE_HEIGHT;

        for (Bucket = 0; Bucket < View->BucketCount; Bucket++)
        {
            const RPERF_TIMELINE_CELL *Cell = CpuMode ?
                RperfTimelineCpuCell(View, Lane, Bucket) :
                RperfTimelineCell(View, Lane, Bucket);
            BOOL Selected;
            LONG Left, Right;
            RECT Marker;

            if (Cell == NULL)
                continue;
            if (Cell->Samples == 0 && Cell->RunningNs == 0 &&
                Cell->ReadyNs == 0 && Cell->WaitingNs == 0 &&
                Cell->ContextSwitches == 0 && Cell->Wakeups == 0 &&
                Cell->ThreadStarts == 0 && Cell->ThreadEnds == 0)
            {
                continue;
            }
            Left = RperfTimelineBucketToX(View, GraphRect, Bucket);
            Right = RperfTimelineBucketToX(View, GraphRect, Bucket + 1);
            if (Right <= Left)
                Right = Left + 1;
            Selected = RperfTimelineModelCellSelected(
                State, View, Lane, Bucket, CpuMode);

            if (CpuMode)
            {
                if (Cell->RunningNs != 0)
                {
                    LONG Height = (LONG)(
                        (double)(RPERF_TIMELINE_LANE_HEIGHT - 7) *
                        min(1.0, (double)Cell->RunningNs /
                                 (double)View->BucketWidthNs));
                    HBRUSH Brush = Selected ? RunningBrush : OutsideBrush;

                    if (Height < 1)
                        Height = 1;
                    Marker.left = Left;
                    Marker.right = Right;
                    Marker.bottom = LaneTop +
                                    RPERF_TIMELINE_LANE_HEIGHT - 3;
                    Marker.top = Marker.bottom - Height;
                    if (Brush != NULL)
                        FillRect(Dc, &Marker, Brush);
                }
                if (Cell->ReadyNs != 0)
                {
                    HBRUSH Brush = Selected ? ReadyBrush : OutsideBrush;

                    Marker.left = Left;
                    Marker.right = Right;
                    Marker.top = LaneTop + 3;
                    Marker.bottom = Marker.top + 2;
                    if (Brush != NULL)
                        FillRect(Dc, &Marker, Brush);
                }
                if (Cell->Samples != 0)
                {
                    HPEN Pen = Selected && SamplePen != NULL ?
                               SamplePen :
                               (OutsidePen != NULL ? OutsidePen :
                                (HPEN)GetStockObject(BLACK_PEN));
                    SelectObject(Dc, Pen);
                    MoveToEx(Dc, Left, LaneTop + 5, NULL);
                    LineTo(Dc, Left,
                           LaneTop + RPERF_TIMELINE_LANE_HEIGHT - 4);
                }
                continue;
            }

            if (Cell->RunningNs != 0 || Cell->ReadyNs != 0 ||
                Cell->WaitingNs != 0)
            {
                HBRUSH Brush;

                if (!Selected)
                    Brush = OutsideBrush;
                else if (Cell->RunningNs >= Cell->ReadyNs &&
                         Cell->RunningNs >= Cell->WaitingNs)
                    Brush = RunningBrush;
                else if (Cell->ReadyNs >= Cell->WaitingNs)
                    Brush = ReadyBrush;
                else
                    Brush = WaitingBrush;
                Marker.left = Left;
                Marker.right = Right;
                Marker.top = LaneTop + 6;
                Marker.bottom = LaneTop +
                                RPERF_TIMELINE_LANE_HEIGHT - 5;
                if (Brush != NULL)
                    FillRect(Dc, &Marker, Brush);
            }
            if (Cell->Samples != 0 && Cell->RunningNs == 0 &&
                Cell->ReadyNs == 0 && Cell->WaitingNs == 0)
            {
                HPEN Pen = Selected && SamplePen != NULL ?
                           SamplePen :
                           (OutsidePen != NULL ? OutsidePen :
                            (HPEN)GetStockObject(BLACK_PEN));
                SelectObject(Dc, Pen);
                MoveToEx(Dc, Left, LaneTop + 5, NULL);
                LineTo(Dc, Left,
                       LaneTop + RPERF_TIMELINE_LANE_HEIGHT - 4);
            }
            if (Cell->ContextSwitches != 0)
            {
                SelectObject(Dc,
                             EventPen != NULL ? EventPen :
                             (HPEN)GetStockObject(BLACK_PEN));
                MoveToEx(Dc, Left, LaneTop + 3, NULL);
                LineTo(Dc, Left,
                       LaneTop + RPERF_TIMELINE_LANE_HEIGHT - 2);
            }
            if (Cell->Wakeups != 0 && WakeupBrush != NULL)
            {
                Marker.left = Left - 1;
                Marker.right = Left + 2;
                Marker.top = LaneTop + 2;
                Marker.bottom = LaneTop + 5;
                FillRect(Dc, &Marker, WakeupBrush);
            }
            if (Cell->ThreadStarts != 0 && StartBrush != NULL)
            {
                Marker.left = Left;
                Marker.right = Left + 2;
                Marker.top = LaneTop + 1;
                Marker.bottom = LaneTop +
                                RPERF_TIMELINE_LANE_HEIGHT - 1;
                FillRect(Dc, &Marker, StartBrush);
            }
            if (Cell->ThreadEnds != 0)
            {
                SelectObject(Dc,
                             EndPen != NULL ? EndPen :
                             (HPEN)GetStockObject(BLACK_PEN));
                MoveToEx(Dc, Left, LaneTop + 1, NULL);
                LineTo(Dc, Left,
                       LaneTop + RPERF_TIMELINE_LANE_HEIGHT - 1);
            }
        }
    }

    SelectObject(Dc, OldPen);
    if (RunningBrush != NULL) DeleteObject(RunningBrush);
    if (ReadyBrush != NULL) DeleteObject(ReadyBrush);
    if (WaitingBrush != NULL) DeleteObject(WaitingBrush);
    if (OutsideBrush != NULL) DeleteObject(OutsideBrush);
    if (WakeupBrush != NULL) DeleteObject(WakeupBrush);
    if (StartBrush != NULL) DeleteObject(StartBrush);
    if (SamplePen != NULL) DeleteObject(SamplePen);
    if (OutsidePen != NULL) DeleteObject(OutsidePen);
    if (EventPen != NULL) DeleteObject(EventPen);
    if (EndPen != NULL) DeleteObject(EndPen);
}

static VOID
RperfTimelineDrawLossMarkers(HDC Dc,
                             const RECT *GraphRect,
                             const RPERF_TIMELINE_VIEW *View,
                             LONG RowsBottom)
{
    HPEN Pen, OldPen;
    SIZE_T Bucket;

    if (View == NULL || View->GlobalLoss == 0 || RowsBottom <= GraphRect->top)
        return;
    Pen = CreatePen(PS_SOLID, 2, RGB(190, 55, 45));
    OldPen = SelectObject(Dc,
                          Pen != NULL ? Pen :
                          (HPEN)GetStockObject(BLACK_PEN));
    for (Bucket = 0; Bucket < View->BucketCount; Bucket++)
    {
        LONG X;

        if (View->LossCells[Bucket] == 0)
            continue;
        X = RperfTimelineBucketToX(View, GraphRect, Bucket);
        MoveToEx(Dc, X, GraphRect->top, NULL);
        LineTo(Dc, X, RowsBottom);
    }
    SelectObject(Dc, OldPen);
    if (Pen != NULL)
        DeleteObject(Pen);
}

static ULONG
RperfTimelineVisibleLaneCount(SIZE_T AvailableLanes,
                              ULONG Capacity,
                              BOOL ShowOverflow)
{
    ULONG VisibleLanes = Capacity;

    if (ShowOverflow && VisibleLanes != 0)
        VisibleLanes--;
    if ((SIZE_T)VisibleLanes > AvailableLanes)
        VisibleLanes = (ULONG)AvailableLanes;
    return VisibleLanes;
}

static VOID
RperfTimelineDraw(HDC Dc,
                  const RECT *Client,
                  RPERF_TIMELINE_STATE *State)
{
    const RPERF_SESSION *Session = State->Session;
    RECT GraphRect, TextRect, ThreadButton, CpuButton;
    ULONGLONG Duration = Session != NULL ? Session->ElapsedUs : 0;
    SIZE_T AvailableLanes;
    ULONG Capacity, VisibleLanes, Lane;
    BOOL CpuMode, PreciseMode, ModelMode, ModelTruncated, ShowOverflow;
    LONG Width, RowsBottom, LegendX;
    HPEN GridPen, BorderPen, OldPen;
    HBRUSH AlternateBrush, SelectionBrush;
    WCHAR Text[256], StartText[64], EndText[64];
    ULONG TickIntervals, Tick;
    RPERF_TIMELINE_COUNTS Counts;
    PCWSTR ModeText;

    FillRect(Dc, Client, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, GetSysColor(COLOR_WINDOWTEXT));
    ZeroMemory(&Counts, sizeof(Counts));
    CpuMode = State->TrackMode == RPERF_TIMELINE_TRACK_CPUS &&
              State->View != NULL && State->View->CpuCount != 0;
    PreciseMode = !CpuMode && State->View != NULL &&
                  State->View->ContextSwitchCount != 0;
    ModelMode = CpuMode || PreciseMode;
    ModelTruncated = ModelMode &&
                     (CpuMode ? State->View->CpusTruncated :
                                State->View->LanesTruncated);
    RperfTimelineGetTrackButtonRectsFromClient(Client,
                                               &ThreadButton,
                                               &CpuButton);

    TextRect = *Client;
    TextRect.left += 8;
    TextRect.top += 3;
    TextRect.right = ThreadButton.left - 6;
    TextRect.bottom = TextRect.top + 18;
    if (Session != NULL)
    {
        RperfTimelineFormatTime(State->Range.StartUs,
                                StartText,
                                ARRAYSIZE(StartText));
        RperfTimelineFormatTime(State->Range.EndUs,
                                EndText,
                                ARRAYSIZE(EndText));
        RperfTimelineCountSamples(State, &Counts);
        if (CpuMode)
        {
            ModeText = State->View->ContextSwitchCount != 0 ?
                       L"Process CPU residency" : L"CPU samples";
        }
        else if (PreciseMode)
        {
            ModeText = L"Scheduler threads";
        }
        else
        {
            ModeText = RperfTimelineModeText(Session);
        }
        _snwprintf(Text,
                   ARRAYSIZE(Text),
                   L"%s | %Iu of %Iu samples in view | %s - %s | drag to filter; double-click to reset",
                   ModeText,
                   Counts.Selected,
                   Session->SampleCount,
                   StartText,
                   EndText);
        Text[ARRAYSIZE(Text) - 1] = UNICODE_NULL;
    }
    else
    {
        lstrcpyW(Text,
                 L"Drag to select a time range; double-click to reset.");
    }
    DrawTextW(Dc,
              Text,
              -1,
              &TextRect,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RperfTimelineDrawTrackButton(
        Dc, &ThreadButton, L"Threads",
        State->TrackMode == RPERF_TIMELINE_TRACK_THREADS,
        Session != NULL);
    RperfTimelineDrawTrackButton(
        Dc, &CpuButton, L"CPUs",
        State->TrackMode == RPERF_TIMELINE_TRACK_CPUS,
        State->View != NULL && State->View->CpuCount != 0);

    if (Session != NULL)
    {
        LegendX = 8;
        if (CpuMode)
        {
            LegendX = RperfTimelineDrawLegendItem(
                Dc, LegendX, 22, RGB(57, 119, 180),
                State->View->ContextSwitchCount != 0 ?
                    L"Running" : L"CPU sample");
            if (State->View->ContextSwitchCount != 0)
            {
                LegendX = RperfTimelineDrawLegendItem(
                    Dc, LegendX, 22, RGB(237, 177, 32), L"Ready target");
            }
        }
        else if (PreciseMode)
        {
            LegendX = RperfTimelineDrawLegendItem(
                Dc, LegendX, 22, RGB(57, 119, 180), L"Running");
            LegendX = RperfTimelineDrawLegendItem(
                Dc, LegendX, 22, RGB(237, 177, 32), L"Ready");
            LegendX = RperfTimelineDrawLegendItem(
                Dc, LegendX, 22, RGB(151, 103, 190), L"Waiting");
            LegendX = RperfTimelineDrawLegendItem(
                Dc, LegendX, 22, RGB(56, 142, 60), L"Wakeup");
        }
        else
        {
            LegendX = RperfTimelineDrawLegendItem(
                Dc, LegendX, 22, RGB(57, 119, 180),
                Session->Backend == RperfBackendIntrusive ?
                    L"Running" : L"CPU sample");
            if (Session->Backend == RperfBackendIntrusive)
            {
                LegendX = RperfTimelineDrawLegendItem(
                    Dc, LegendX, 22,
                    (Session->FilterFlags & RPERF_FILTER_CPU_ONLY) != 0 ?
                        RGB(226, 197, 155) : RGB(225, 146, 45),
                    (Session->FilterFlags & RPERF_FILTER_CPU_ONLY) != 0 ?
                        L"Waiting excluded" : L"Waiting");
            }
            if (Counts.Unknown != 0)
            {
                LegendX = RperfTimelineDrawLegendItem(
                    Dc, LegendX, 22,
                    RGB(99, 118, 135), L"State unknown");
            }
            if (Counts.Diagnostic != 0)
            {
                LegendX = RperfTimelineDrawLegendItem(
                    Dc, LegendX, 22,
                    RGB(190, 55, 45), L"Capture issue");
            }
        }
        LegendX = RperfTimelineDrawLegendItem(
            Dc, LegendX, 22, RGB(194, 202, 211), L"Outside filter");
        if (State->View != NULL && State->View->GlobalLoss != 0)
        {
            RperfTimelineDrawLegendItem(
                Dc, LegendX, 22, RGB(190, 55, 45), L"Lost data");
        }
    }

    GraphRect.left = RPERF_TIMELINE_LABEL_WIDTH;
    GraphRect.top = RPERF_TIMELINE_HEADER_HEIGHT;
    GraphRect.right = Client->right - RPERF_TIMELINE_RIGHT_MARGIN;
    GraphRect.bottom = Client->bottom;
    Width = GraphRect.right - GraphRect.left;
    if (Width <= 0 || GraphRect.bottom <= GraphRect.top)
        return;

    Capacity = (ULONG)((GraphRect.bottom - GraphRect.top) /
                       RPERF_TIMELINE_LANE_HEIGHT);
    AvailableLanes = ModelMode ?
                     (CpuMode ? State->View->CpuCount :
                                State->View->LaneCount) :
                     State->LaneCount;
    ShowOverflow = ModelTruncated || AvailableLanes > Capacity ||
                   (!ModelMode && State->ThreadCount > State->LaneCount);
    VisibleLanes = RperfTimelineVisibleLaneCount(AvailableLanes,
                                                Capacity,
                                                ShowOverflow);

    AlternateBrush = CreateSolidBrush(RGB(247, 249, 252));
    for (Lane = 0; Lane < VisibleLanes; Lane++)
    {
        RECT LaneRect;

        LaneRect.left = Client->left;
        LaneRect.top = GraphRect.top +
                       (LONG)Lane * RPERF_TIMELINE_LANE_HEIGHT;
        LaneRect.right = GraphRect.right;
        LaneRect.bottom = LaneRect.top + RPERF_TIMELINE_LANE_HEIGHT;
        if ((Lane & 1) != 0 && AlternateBrush != NULL)
            FillRect(Dc, &LaneRect, AlternateBrush);

        if (CpuMode)
        {
            _snwprintf(Text,
                       ARRAYSIZE(Text),
                       L"CPU %lu",
                       State->View->CpuIds[Lane]);
        }
        else if (PreciseMode && State->View->Lanes[Lane].ThreadId != 0)
        {
            _snwprintf(Text,
                       ARRAYSIZE(Text),
                       L"TID %lu",
                       State->View->Lanes[Lane].ThreadId);
        }
        else if (PreciseMode)
        {
            _snwprintf(Text,
                       ARRAYSIZE(Text),
                       L"Thread %I64u",
                       State->View->Lanes[Lane].ThreadKey);
        }
        else
        {
            _snwprintf(Text,
                       ARRAYSIZE(Text),
                       L"TID %lu",
                       State->ThreadIds[Lane]);
        }
        Text[ARRAYSIZE(Text) - 1] = UNICODE_NULL;
        TextRect = LaneRect;
        TextRect.left += 4;
        TextRect.right = GraphRect.left - 6;
        DrawTextW(Dc,
                  Text,
                  -1,
                  &TextRect,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    }
    if (AlternateBrush != NULL)
        DeleteObject(AlternateBrush);

    RowsBottom = GraphRect.top +
                 (LONG)VisibleLanes * RPERF_TIMELINE_LANE_HEIGHT;
    if (VisibleLanes != 0)
    {
        LONG StartX = GraphRect.left +
            RperfTimelineTimeToOffset(State->Range.StartUs, Duration, Width);
        LONG EndX = GraphRect.left +
            RperfTimelineTimeToOffset(State->Range.EndUs, Duration, Width);
        RECT SelectionRect;

        SelectionRect.left = StartX;
        SelectionRect.top = GraphRect.top;
        SelectionRect.right = EndX;
        SelectionRect.bottom = RowsBottom;
        if (SelectionRect.right <= SelectionRect.left)
            SelectionRect.right = SelectionRect.left + 1;
        if (SelectionRect.right > GraphRect.right + 1)
            SelectionRect.right = GraphRect.right + 1;
        SelectionBrush = CreateSolidBrush(RGB(220, 235, 250));
        if (SelectionBrush != NULL)
        {
            FillRect(Dc, &SelectionRect, SelectionBrush);
            DeleteObject(SelectionBrush);
        }
    }

    GridPen = CreatePen(PS_DOT, 1, RGB(198, 205, 214));
    BorderPen = CreatePen(PS_SOLID, 1, RGB(120, 128, 138));
    OldPen = SelectObject(Dc,
                          GridPen != NULL ? GridPen :
                          (HPEN)GetStockObject(BLACK_PEN));

    TickIntervals = (ULONG)(Width / RPERF_TIMELINE_TICK_SPACING);
    if (TickIntervals < 2)
        TickIntervals = 2;
    if (TickIntervals > 10)
        TickIntervals = 10;
    if (Duration == 0)
        TickIntervals = 0;

    for (Tick = 0; Tick <= TickIntervals; Tick++)
    {
        LONG X;
        ULONGLONG TickTime;

        if (TickIntervals == 0)
        {
            X = GraphRect.left;
            TickTime = 0;
        }
        else
        {
            X = GraphRect.left +
                (LONG)(((LONGLONG)Width * Tick) / TickIntervals);
            TickTime = (Duration / TickIntervals) * Tick +
                       ((Duration % TickIntervals) * Tick) / TickIntervals;
        }

        MoveToEx(Dc, X, RPERF_TIMELINE_HEADER_HEIGHT - 5, NULL);
        LineTo(Dc, X, RowsBottom);

        RperfTimelineFormatTime(TickTime, Text, ARRAYSIZE(Text));
        TextRect.left = X - 54;
        TextRect.top = 44;
        TextRect.right = X + 54;
        TextRect.bottom = RPERF_TIMELINE_HEADER_HEIGHT - 6;
        if (TextRect.left < GraphRect.left)
        {
            TextRect.right += GraphRect.left - TextRect.left;
            TextRect.left = GraphRect.left;
        }
        if (TextRect.right > GraphRect.right)
        {
            TextRect.left -= TextRect.right - GraphRect.right;
            TextRect.right = GraphRect.right;
        }
        DrawTextW(Dc,
                  Text,
                  -1,
                  &TextRect,
                  DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (TickIntervals == 0)
            break;
    }

    if (BorderPen != NULL)
        SelectObject(Dc, BorderPen);
    MoveToEx(Dc,
             GraphRect.left,
             RPERF_TIMELINE_HEADER_HEIGHT - 1,
             NULL);
    LineTo(Dc, GraphRect.right, RPERF_TIMELINE_HEADER_HEIGHT - 1);
    for (Lane = 0; Lane <= VisibleLanes; Lane++)
    {
        LONG Y = GraphRect.top +
                 (LONG)Lane * RPERF_TIMELINE_LANE_HEIGHT;
        MoveToEx(Dc, GraphRect.left, Y, NULL);
        LineTo(Dc, GraphRect.right, Y);
    }

    SelectObject(Dc, OldPen);
    if (GridPen != NULL)
        DeleteObject(GridPen);
    if (BorderPen != NULL)
        DeleteObject(BorderPen);

    if (Session == NULL || Session->SampleCount == 0 ||
        Session->Samples == NULL)
    {
        TextRect = GraphRect;
        DrawTextW(Dc,
                  L"Record a process or open an .rperf log to view its timeline.",
                  -1,
                  &TextRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    if (ModelMode)
    {
        RperfTimelineDrawModelMarkers(Dc,
                                      &GraphRect,
                                      State,
                                      VisibleLanes,
                                      CpuMode);
    }
    else
    {
        RperfTimelineDrawMarkers(Dc, &GraphRect, State, VisibleLanes);
    }
    RperfTimelineDrawLossMarkers(Dc,
                                 &GraphRect,
                                 State->View,
                                 RowsBottom);

    if (VisibleLanes != 0)
    {
        HPEN SelectionPen = CreatePen(PS_SOLID, 1, RGB(24, 101, 180));
        LONG StartX = GraphRect.left +
            RperfTimelineTimeToOffset(State->Range.StartUs, Duration, Width);
        LONG EndX = GraphRect.left +
            RperfTimelineTimeToOffset(State->Range.EndUs, Duration, Width);

        if (SelectionPen != NULL)
        {
            OldPen = SelectObject(Dc, SelectionPen);
            MoveToEx(Dc, StartX, GraphRect.top, NULL);
            LineTo(Dc, StartX, RowsBottom);
            if (EndX != StartX)
            {
                MoveToEx(Dc, EndX, GraphRect.top, NULL);
                LineTo(Dc, EndX, RowsBottom);
            }
            SelectObject(Dc, OldPen);
            DeleteObject(SelectionPen);
        }
    }

    if (ShowOverflow && Capacity != 0)
    {
        RECT OverflowRect;

        OverflowRect.left = 4;
        OverflowRect.top = RowsBottom;
        OverflowRect.right = GraphRect.right;
        OverflowRect.bottom = OverflowRect.top + RPERF_TIMELINE_LANE_HEIGHT;
        FillRect(Dc, &OverflowRect, GetSysColorBrush(COLOR_BTNFACE));
        if (ModelMode && ModelTruncated)
        {
            _snwprintf(Text,
                       ARRAYSIZE(Text),
                       CpuMode ?
                       L"Additional CPU tracks not shown (64-track limit)." :
                       L"Additional thread tracks not shown (64-track limit).");
        }
        else if (ModelMode)
        {
            ULONGLONG Additional = (ULONGLONG)
                (AvailableLanes - VisibleLanes);

            _snwprintf(Text,
                       ARRAYSIZE(Text),
                       CpuMode ?
                       (Additional == 1 ?
                        L"+%I64u additional CPU not shown" :
                        L"+%I64u additional CPUs not shown") :
                       (Additional == 1 ?
                        L"+%I64u additional thread not shown" :
                        L"+%I64u additional threads not shown"),
                       Additional);
        }
        else if (State->ThreadCountExact)
        {
            ULONGLONG Additional = (ULONGLONG)
                (State->ThreadCount - VisibleLanes);
            _snwprintf(Text,
                       ARRAYSIZE(Text),
                       Additional == 1 ?
                       L"+%I64u additional thread not shown" :
                       L"+%I64u additional threads not shown",
                       Additional);
        }
        else
        {
            lstrcpyW(Text,
                     L"Additional threads not shown (not enough memory to count them).");
        }
        Text[ARRAYSIZE(Text) - 1] = UNICODE_NULL;
        DrawTextW(Dc,
                  Text,
                  -1,
                  &OverflowRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    }
}

static VOID
RperfTimelinePaint(HWND Window,
                   RPERF_TIMELINE_STATE *State)
{
    PAINTSTRUCT Paint;
    RECT Client;
    HDC Dc, MemoryDc;
    HBITMAP Bitmap, OldBitmap;
    HFONT OldFont;

    Dc = BeginPaint(Window, &Paint);
    GetClientRect(Window, &Client);
    if (IsRectEmpty(&Client))
    {
        EndPaint(Window, &Paint);
        return;
    }

    MemoryDc = CreateCompatibleDC(Dc);
    Bitmap = MemoryDc != NULL ?
             CreateCompatibleBitmap(Dc,
                                    Client.right - Client.left,
                                    Client.bottom - Client.top) : NULL;
    if (MemoryDc != NULL && Bitmap != NULL)
    {
        OldBitmap = SelectObject(MemoryDc, Bitmap);
        OldFont = SelectObject(MemoryDc, GetStockObject(DEFAULT_GUI_FONT));
        RperfTimelineDraw(MemoryDc, &Client, State);
        BitBlt(Dc,
               Client.left,
               Client.top,
               Client.right - Client.left,
               Client.bottom - Client.top,
               MemoryDc,
               0,
               0,
               SRCCOPY);
        SelectObject(MemoryDc, OldFont);
        SelectObject(MemoryDc, OldBitmap);
    }
    else
    {
        OldFont = SelectObject(Dc, GetStockObject(DEFAULT_GUI_FONT));
        RperfTimelineDraw(Dc, &Client, State);
        SelectObject(Dc, OldFont);
    }

    if (Bitmap != NULL)
        DeleteObject(Bitmap);
    if (MemoryDc != NULL)
        DeleteDC(MemoryDc);
    EndPaint(Window, &Paint);
}

static VOID
RperfTimelineNotifyRange(HWND Window,
                         const RPERF_TIMELINE_STATE *State)
{
    RPERF_TIME_RANGE Range = State->Range;
    HWND Root = GetAncestor(Window, GA_ROOT);

    if (Root != NULL)
    {
        SendMessageW(Root,
                     WM_RPERF_TIMELINE_RANGE_CHANGED,
                     0,
                     (LPARAM)&Range);
    }
}

static VOID
RperfTimelineClearHover(HWND Window,
                        RPERF_TIMELINE_STATE *State)
{
    RPERF_TIMELINE_HOVER Hover;
    HWND Root;

    if (State->HoverLane == (SIZE_T)-1 &&
        State->HoverBucket == (SIZE_T)-1)
    {
        return;
    }
    State->HoverLane = (SIZE_T)-1;
    State->HoverBucket = (SIZE_T)-1;
    ZeroMemory(&Hover, sizeof(Hover));
    Root = GetAncestor(Window, GA_ROOT);
    if (Root != NULL)
    {
        SendMessageW(Root,
                     WM_RPERF_TIMELINE_HOVER,
                     0,
                     (LPARAM)&Hover);
    }
}

static VOID
RperfTimelineNotifyHover(HWND Window,
                         RPERF_TIMELINE_STATE *State,
                         LONG X,
                         LONG Y)
{
    RPERF_TIMELINE_HOVER Hover;
    const RPERF_TIMELINE_CELL *Cell;
    const RPERF_TIMELINE_VIEW *View = State->View;
    RECT GraphRect;
    BOOL CpuMode, PreciseMode, ModelTruncated, ShowOverflow;
    SIZE_T Lane, Bucket, LaneCount;
    ULONG Capacity, VisibleLanes;
    LONG Width;
    HWND Root;

    CpuMode = State->TrackMode == RPERF_TIMELINE_TRACK_CPUS &&
              View != NULL && View->CpuCount != 0;
    PreciseMode = !CpuMode && View != NULL &&
                  View->ContextSwitchCount != 0;
    if ((!CpuMode && !PreciseMode) ||
        !RperfTimelineGetGraphRect(Window, &GraphRect) ||
        X < GraphRect.left || X >= GraphRect.right ||
        Y < GraphRect.top || Y >= GraphRect.bottom)
    {
        RperfTimelineClearHover(Window, State);
        return;
    }
    Lane = (SIZE_T)((Y - GraphRect.top) / RPERF_TIMELINE_LANE_HEIGHT);
    LaneCount = CpuMode ? View->CpuCount : View->LaneCount;
    ModelTruncated = CpuMode ? View->CpusTruncated : View->LanesTruncated;
    Capacity = (ULONG)((GraphRect.bottom - GraphRect.top) /
                       RPERF_TIMELINE_LANE_HEIGHT);
    ShowOverflow = ModelTruncated || LaneCount > Capacity;
    VisibleLanes = RperfTimelineVisibleLaneCount(LaneCount,
                                                Capacity,
                                                ShowOverflow);
    if (Lane >= VisibleLanes)
    {
        RperfTimelineClearHover(Window, State);
        return;
    }
    Width = GraphRect.right - GraphRect.left;
    Bucket = (SIZE_T)RperfTimelineOffsetToTime(X - GraphRect.left,
                                               Width,
                                               View->BucketCount);
    if (Bucket >= View->BucketCount)
        Bucket = View->BucketCount - 1;
    if (State->HoverLane == Lane && State->HoverBucket == Bucket)
        return;
    Cell = CpuMode ? RperfTimelineCpuCell(View, Lane, Bucket) :
                     RperfTimelineCell(View, Lane, Bucket);
    if (Cell == NULL)
    {
        RperfTimelineClearHover(Window, State);
        return;
    }

    ZeroMemory(&Hover, sizeof(Hover));
    Hover.Active = TRUE;
    Hover.TrackMode = CpuMode ? RPERF_TIMELINE_TRACK_CPUS :
                                RPERF_TIMELINE_TRACK_THREADS;
    Hover.StartUs = (Bucket * View->BucketWidthNs) / 1000;
    Hover.EndUs = ((Bucket + 1) * View->BucketWidthNs) / 1000;
    if (Hover.EndUs > State->Session->ElapsedUs)
        Hover.EndUs = State->Session->ElapsedUs;
    if (!CpuMode)
    {
        Hover.ProcessId = View->Lanes[Lane].ProcessId;
        Hover.ThreadId = View->Lanes[Lane].ThreadId;
    }
    Hover.Cpu = CpuMode ? View->CpuIds[Lane] : Cell->Cpu;
    Hover.WaitReason = Cell->WaitReason;
    if (Cell->Flags & RPERF_TIMELINE_CELL_MIXED_REASON)
        Hover.Flags |= RPERF_TIMELINE_HOVER_MIXED_REASON;
    if (Cell->Flags & RPERF_TIMELINE_CELL_MIXED_CPU)
        Hover.Flags |= RPERF_TIMELINE_HOVER_MIXED_CPU;
    if (View->ContextSwitchCount != 0)
        Hover.Flags |= RPERF_TIMELINE_HOVER_PRECISE;
    Hover.Samples = Cell->Samples;
    Hover.RunningNs = Cell->RunningNs;
    Hover.ReadyNs = Cell->ReadyNs;
    Hover.WaitingNs = Cell->WaitingNs;
    Hover.ContextSwitches = Cell->ContextSwitches;
    Hover.Wakeups = Cell->Wakeups;
    Hover.Loss = View->LossCells[Bucket];
    State->HoverLane = Lane;
    State->HoverBucket = Bucket;
    Root = GetAncestor(Window, GA_ROOT);
    if (Root != NULL)
    {
        SendMessageW(Root,
                     WM_RPERF_TIMELINE_HOVER,
                     0,
                     (LPARAM)&Hover);
    }
}

static VOID
RperfTimelineCancelDrag(HWND Window,
                        RPERF_TIMELINE_STATE *State)
{
    if (State->Dragging)
    {
        State->Range = State->DragOriginalRange;
        State->CountsValid = FALSE;
    }
    State->Dragging = FALSE;
    State->DragAnchorUs = 0;
    if (GetCapture() == Window)
        ReleaseCapture();
}

static LRESULT CALLBACK
RperfTimelineWndProc(HWND Window,
                     UINT Message,
                     WPARAM WParam,
                     LPARAM LParam)
{
    RPERF_TIMELINE_STATE *State =
        (RPERF_TIMELINE_STATE *)GetWindowLongPtrW(Window, GWLP_USERDATA);

    switch (Message)
    {
        case WM_CREATE:
            State = HeapAlloc(GetProcessHeap(),
                              HEAP_ZERO_MEMORY,
                              sizeof(*State));
            if (State == NULL)
                return -1;
            State->ThreadCountExact = TRUE;
            State->TrackMode = RPERF_TIMELINE_TRACK_THREADS;
            State->HoverLane = (SIZE_T)-1;
            State->HoverBucket = (SIZE_T)-1;
            SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)State);
            return 0;

        case WM_DESTROY:
            if (State != NULL)
            {
                RperfTimelineCancelDrag(Window, State);
                RperfTimelineSetView(State, NULL);
                HeapFree(GetProcessHeap(), 0, State);
                SetWindowLongPtrW(Window, GWLP_USERDATA, 0);
            }
            return 0;

        case WM_RPERF_TIMELINE_SET_VIEW:
            if (State != NULL)
            {
                RperfTimelineCancelDrag(Window, State);
                RperfTimelineClearHover(Window, State);
                RperfTimelineSetView(
                    State, (RPERF_TIMELINE_VIEW *)LParam);
                InvalidateRect(Window, NULL, FALSE);
                return TRUE;
            }
            return FALSE;

        case WM_RPERF_TIMELINE_GET_BUCKET_COUNT:
            return RperfTimelineBucketCount(Window);

        case WM_RPERF_TIMELINE_SET_TRACK_MODE:
            if (State != NULL &&
                (WParam == RPERF_TIMELINE_TRACK_THREADS ||
                 (WParam == RPERF_TIMELINE_TRACK_CPUS &&
                  State->View != NULL && State->View->CpuCount != 0)))
            {
                RperfTimelineClearHover(Window, State);
                State->TrackMode = (ULONG)WParam;
                InvalidateRect(Window, NULL, FALSE);
                return TRUE;
            }
            return FALSE;

        case WM_RPERF_TIMELINE_GET_TRACK_MODE:
            return State != NULL ? State->TrackMode :
                                   RPERF_TIMELINE_TRACK_THREADS;

        case WM_RPERF_TIMELINE_SET_SESSION:
            if (State != NULL)
            {
                RPERF_TIME_RANGE Range;

                RperfTimelineCancelDrag(Window, State);
                State->Session = (const RPERF_SESSION *)LParam;
                RperfTimelineBuildLanes(State);
                if (State->Session != NULL)
                {
                    Range.StartUs = State->Session->FilterStartUs;
                    Range.EndUs = State->Session->FilterEndUs;
                }
                else
                {
                    Range.StartUs = 0;
                    Range.EndUs = 0;
                }
                RperfTimelineSetRange(State, &Range);
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;

        case WM_RPERF_TIMELINE_SET_RANGE:
            if (State != NULL && LParam != 0)
            {
                RPERF_TIME_RANGE Range = *(const RPERF_TIME_RANGE *)LParam;

                RperfTimelineSetRange(State, &Range);
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;

        case WM_RPERF_TIMELINE_GET_GRAPH_ORIGIN:
            return MAKELRESULT(RPERF_TIMELINE_LABEL_WIDTH,
                               RPERF_TIMELINE_HEADER_HEIGHT);

        case WM_RPERF_TIMELINE_GET_STAT:
            if (State != NULL && State->Session != NULL)
            {
                RPERF_TIMELINE_COUNTS Counts;

                switch (WParam)
                {
                    case RPERF_TIMELINE_STAT_TOTAL:
                        return State->Session->SampleCount;
                    case RPERF_TIMELINE_STAT_CPU_ONLY:
                        return (State->Session->FilterFlags &
                                RPERF_FILTER_CPU_ONLY) != 0;
                    case RPERF_TIMELINE_STAT_CONTEXT_SWITCHES:
                        return State->View != NULL ?
                               State->View->ContextSwitchCount : 0;
                    case RPERF_TIMELINE_STAT_WAKEUPS:
                        return State->View != NULL ?
                               State->View->WakeupCount : 0;
                    case RPERF_TIMELINE_STAT_LOSS:
                        return State->View != NULL ?
                               State->View->GlobalLoss : 0;
                    case RPERF_TIMELINE_STAT_CPU_COUNT:
                        return State->View != NULL ?
                               State->View->CpuCount : 0;
                    case RPERF_TIMELINE_STAT_HAS_SCHEDULER:
                        return State->View != NULL &&
                               State->View->HasScheduler;
                }
                if (WParam != RPERF_TIMELINE_STAT_SELECTED &&
                    WParam != RPERF_TIMELINE_STAT_RUNNING &&
                    WParam != RPERF_TIMELINE_STAT_WAITING &&
                    WParam != RPERF_TIMELINE_STAT_UNKNOWN &&
                    WParam != RPERF_TIMELINE_STAT_DIAGNOSTIC &&
                    WParam != RPERF_TIMELINE_STAT_WAITING_EXCLUDED)
                {
                    return 0;
                }
                RperfTimelineCountSamples(State, &Counts);
                switch (WParam)
                {
                    case RPERF_TIMELINE_STAT_SELECTED:
                        return Counts.Selected;
                    case RPERF_TIMELINE_STAT_RUNNING:
                        return Counts.Running;
                    case RPERF_TIMELINE_STAT_WAITING:
                        return Counts.Waiting;
                    case RPERF_TIMELINE_STAT_UNKNOWN:
                        return Counts.Unknown;
                    case RPERF_TIMELINE_STAT_DIAGNOSTIC:
                        return Counts.Diagnostic;
                    case RPERF_TIMELINE_STAT_WAITING_EXCLUDED:
                        return Counts.WaitingExcluded;
                }
            }
            return 0;

        case WM_PAINT:
            if (State != NULL)
                RperfTimelinePaint(Window, State);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            InvalidateRect(Window, NULL, FALSE);
            return 0;

        case WM_LBUTTONDOWN:
            if (State != NULL && State->Session != NULL)
            {
                RECT GraphRect;
                RECT ThreadRect, CpuRect;
                POINT Point;
                LONG X = GET_X_LPARAM(LParam);
                LONG Y = GET_Y_LPARAM(LParam);

                Point.x = X;
                Point.y = Y;
                RperfTimelineGetTrackButtonRects(Window,
                                                 &ThreadRect,
                                                 &CpuRect);
                if (PtInRect(&ThreadRect, Point))
                {
                    if (SendMessageW(Window, WM_RPERF_TIMELINE_SET_TRACK_MODE, RPERF_TIMELINE_TRACK_THREADS, 0))
                    {
                        SetFocus(Window);
                    }
                    return 0;
                }
                if (PtInRect(&CpuRect, Point))
                {
                    if (SendMessageW(Window, WM_RPERF_TIMELINE_SET_TRACK_MODE, RPERF_TIMELINE_TRACK_CPUS, 0))
                    {
                        SetFocus(Window);
                    }
                    return 0;
                }

                if (RperfTimelineGetGraphRect(Window, &GraphRect) &&
                    X >= GraphRect.left && X <= GraphRect.right &&
                    Y >= GraphRect.top && Y < GraphRect.bottom)
                {
                    RperfTimelineClearHover(Window, State);
                    State->DragAnchorUs =
                        RperfTimelinePointToTime(State, &GraphRect, X);
                    State->DragOriginalRange = State->Range;
                    State->Range.StartUs = State->DragAnchorUs;
                    State->Range.EndUs = State->DragAnchorUs;
                    State->CountsValid = FALSE;
                    State->Dragging = TRUE;
                    SetFocus(Window);
                    SetCapture(Window);
                    InvalidateRect(Window, NULL, FALSE);
                }
            }
            return 0;

        case WM_MOUSEMOVE:
            if (State != NULL && !State->TrackingMouse)
            {
                TRACKMOUSEEVENT Track;

                ZeroMemory(&Track, sizeof(Track));
                Track.cbSize = sizeof(Track);
                Track.dwFlags = TME_LEAVE;
                Track.hwndTrack = Window;
                if (TrackMouseEvent(&Track))
                    State->TrackingMouse = TRUE;
            }
            if (State != NULL && State->Dragging && State->Session != NULL)
            {
                RECT GraphRect;

                if (RperfTimelineGetGraphRect(Window, &GraphRect))
                {
                    ULONGLONG Current = RperfTimelinePointToTime(
                        State,
                        &GraphRect,
                        GET_X_LPARAM(LParam));

                    if (Current < State->DragAnchorUs)
                    {
                        State->Range.StartUs = Current;
                        State->Range.EndUs = State->DragAnchorUs;
                    }
                    else
                    {
                        State->Range.StartUs = State->DragAnchorUs;
                        State->Range.EndUs = Current;
                    }
                    State->CountsValid = FALSE;
                    InvalidateRect(Window, NULL, FALSE);
                }
            }
            else if (State != NULL && State->Session != NULL)
            {
                RperfTimelineNotifyHover(Window,
                                         State,
                                         GET_X_LPARAM(LParam),
                                         GET_Y_LPARAM(LParam));
            }
            return 0;

        case WM_MOUSELEAVE:
            if (State != NULL)
            {
                State->TrackingMouse = FALSE;
                RperfTimelineClearHover(Window, State);
            }
            return 0;

        case WM_KEYDOWN:
            if (State != NULL && (WParam == 'T' || WParam == 'C'))
            {
                ULONG Mode = WParam == 'C' ?
                             RPERF_TIMELINE_TRACK_CPUS :
                             RPERF_TIMELINE_TRACK_THREADS;

                SendMessageW(Window,
                             WM_RPERF_TIMELINE_SET_TRACK_MODE,
                             Mode,
                             0);
            }
            return 0;

        case WM_LBUTTONUP:
            if (State != NULL && State->Dragging && State->Session != NULL)
            {
                RECT GraphRect;

                if (RperfTimelineGetGraphRect(Window, &GraphRect))
                {
                    ULONGLONG Current = RperfTimelinePointToTime(
                        State,
                        &GraphRect,
                        GET_X_LPARAM(LParam));

                    if (Current < State->DragAnchorUs)
                    {
                        State->Range.StartUs = Current;
                        State->Range.EndUs = State->DragAnchorUs;
                    }
                    else
                    {
                        State->Range.StartUs = State->DragAnchorUs;
                        State->Range.EndUs = Current;
                    }
                    State->CountsValid = FALSE;
                }

                State->Dragging = FALSE;
                State->DragAnchorUs = 0;
                if (GetCapture() == Window)
                    ReleaseCapture();
                InvalidateRect(Window, NULL, FALSE);
                RperfTimelineNotifyRange(Window, State);
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (State != NULL && State->Session != NULL)
            {
                RPERF_TIME_RANGE Range;

                RperfTimelineCancelDrag(Window, State);
                Range.StartUs = 0;
                Range.EndUs = State->Session->ElapsedUs;
                RperfTimelineSetRange(State, &Range);
                InvalidateRect(Window, NULL, FALSE);
                RperfTimelineNotifyRange(Window, State);
            }
            return 0;

        case WM_CANCELMODE:
            if (State != NULL)
            {
                RperfTimelineCancelDrag(Window, State);
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;

        case WM_CAPTURECHANGED:
            if (State != NULL && (HWND)LParam != Window)
            {
                RperfTimelineCancelDrag(Window, State);
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

BOOL
RperfRegisterTimeline(HINSTANCE Instance)
{
    WNDCLASSEXW Class;

    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    Class.lpfnWndProc = RperfTimelineWndProc;
    Class.hInstance = Instance;
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    Class.lpszClassName = RPERF_TIMELINE_CLASS;
    return RegisterClassExW(&Class) != 0;
}

VOID
RperfUnregisterTimeline(HINSTANCE Instance)
{
    UnregisterClassW(RPERF_TIMELINE_CLASS, Instance);
}
