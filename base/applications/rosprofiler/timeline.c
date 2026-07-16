/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Interactive thread timeline control
 */

#include "rosprofiler.h"

#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>

#define RPERF_TIMELINE_HEADER_HEIGHT 48
#define RPERF_TIMELINE_LABEL_WIDTH 88
#define RPERF_TIMELINE_RIGHT_MARGIN 12
#define RPERF_TIMELINE_LANE_HEIGHT 24
#define RPERF_TIMELINE_MAX_LANES 64
#define RPERF_TIMELINE_TICK_SPACING 120

#define RPERF_TIMELINE_BUCKET_ERROR 0x80
#define RPERF_TIMELINE_BUCKET_COUNT 0x7f

typedef struct _RPERF_TIMELINE_STATE
{
    const RPERF_SESSION *Session;
    RPERF_TIME_RANGE Range;
    DWORD ThreadIds[RPERF_TIMELINE_MAX_LANES];
    SIZE_T ThreadCount;
    ULONG LaneCount;
    BOOL ThreadCountExact;
    BOOL Dragging;
    ULONGLONG DragAnchorUs;
    RPERF_TIME_RANGE DragOriginalRange;
} RPERF_TIMELINE_STATE;

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
RperfTimelineDrawMarkers(HDC Dc,
                         const RECT *GraphRect,
                         const RPERF_TIMELINE_STATE *State,
                         ULONG VisibleLanes)
{
    const RPERF_SESSION *Session = State->Session;
    LONG Width = GraphRect->right - GraphRect->left;
    SIZE_T Columns = (SIZE_T)Width + 1;
    SIZE_T BucketCount;
    BYTE *Buckets = NULL;
    HPEN Pens[4];
    HPEN OldPen, CurrentPen;
    SIZE_T SampleIndex;
    ULONG Lane;

    if (Session == NULL || Session->Samples == NULL ||
        Session->SampleCount == 0 || VisibleLanes == 0 || Width <= 0)
    {
        return;
    }

    if (Columns <= ((SIZE_T)-1) / VisibleLanes)
    {
        BucketCount = Columns * VisibleLanes;
        Buckets = HeapAlloc(GetProcessHeap(),
                            HEAP_ZERO_MEMORY,
                            BucketCount);
    }

    Pens[0] = CreatePen(PS_SOLID, 1, RGB(102, 157, 207));
    Pens[1] = CreatePen(PS_SOLID, 1, RGB(57, 119, 180));
    Pens[2] = CreatePen(PS_SOLID, 1, RGB(21, 82, 143));
    Pens[3] = CreatePen(PS_SOLID, 1, RGB(190, 55, 45));
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
            BYTE Bucket, Count;

            Lane = RperfTimelineFindLane(State,
                                         Sample->ThreadId,
                                         VisibleLanes);
            if (Lane == RPERF_TIMELINE_MAX_LANES)
                continue;

            Offset = RperfTimelineTimeToOffset(Sample->TimeUs,
                                               Session->ElapsedUs,
                                               Width);
            BucketIndex = (SIZE_T)Lane * Columns + (SIZE_T)Offset;
            Bucket = Buckets[BucketIndex];
            Count = Bucket & RPERF_TIMELINE_BUCKET_COUNT;
            if (Count != RPERF_TIMELINE_BUCKET_COUNT)
                Count++;
            if (Sample->Flags)
                Bucket |= RPERF_TIMELINE_BUCKET_ERROR;
            Buckets[BucketIndex] = Count |
                                   (Bucket & RPERF_TIMELINE_BUCKET_ERROR);
        }

        for (Lane = 0; Lane < VisibleLanes; Lane++)
        {
            LONG LaneTop = GraphRect->top +
                           (LONG)Lane * RPERF_TIMELINE_LANE_HEIGHT;
            SIZE_T Column;

            for (Column = 0; Column < Columns; Column++)
            {
                BYTE Bucket = Buckets[(SIZE_T)Lane * Columns + Column];
                HPEN Pen;

                if (Bucket == 0)
                    continue;
                if (Bucket & RPERF_TIMELINE_BUCKET_ERROR)
                    Pen = Pens[3];
                else if ((Bucket & RPERF_TIMELINE_BUCKET_COUNT) >= 8)
                    Pen = Pens[2];
                else if ((Bucket & RPERF_TIMELINE_BUCKET_COUNT) >= 3)
                    Pen = Pens[1];
                else
                    Pen = Pens[0];
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
            HPEN Pen = Sample->Flags ? Pens[3] : Pens[0];
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

static VOID
RperfTimelineDraw(HDC Dc,
                  const RECT *Client,
                  RPERF_TIMELINE_STATE *State)
{
    const RPERF_SESSION *Session = State->Session;
    RECT GraphRect, TextRect;
    ULONGLONG Duration = Session != NULL ? Session->ElapsedUs : 0;
    ULONG Capacity, VisibleLanes, Lane;
    BOOL ShowOverflow;
    LONG Width, RowsBottom;
    HPEN GridPen, BorderPen, OldPen;
    HBRUSH AlternateBrush, SelectionBrush;
    WCHAR Text[256], StartText[64], EndText[64];
    ULONG TickIntervals, Tick;

    FillRect(Dc, Client, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, GetSysColor(COLOR_WINDOWTEXT));

    TextRect = *Client;
    TextRect.left += 8;
    TextRect.top += 3;
    TextRect.right -= 8;
    TextRect.bottom = TextRect.top + 18;
    if (Session != NULL)
    {
        RperfTimelineFormatTime(State->Range.StartUs,
                                StartText,
                                ARRAYSIZE(StartText));
        RperfTimelineFormatTime(State->Range.EndUs,
                                EndText,
                                ARRAYSIZE(EndText));
        _snwprintf(Text,
                   ARRAYSIZE(Text),
                   L"Drag to select a time range; double-click to reset.  Selected: %s - %s",
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

    GraphRect.left = RPERF_TIMELINE_LABEL_WIDTH;
    GraphRect.top = RPERF_TIMELINE_HEADER_HEIGHT;
    GraphRect.right = Client->right - RPERF_TIMELINE_RIGHT_MARGIN;
    GraphRect.bottom = Client->bottom;
    Width = GraphRect.right - GraphRect.left;
    if (Width <= 0 || GraphRect.bottom <= GraphRect.top)
        return;

    Capacity = (ULONG)((GraphRect.bottom - GraphRect.top) /
                       RPERF_TIMELINE_LANE_HEIGHT);
    ShowOverflow = State->ThreadCount > State->LaneCount ||
                   State->ThreadCount > Capacity;
    if (ShowOverflow)
    {
        VisibleLanes = Capacity > 0 ? Capacity - 1 : 0;
        if (VisibleLanes > State->LaneCount)
            VisibleLanes = State->LaneCount;
    }
    else
    {
        VisibleLanes = Capacity;
        if (VisibleLanes > State->LaneCount)
            VisibleLanes = State->LaneCount;
    }

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

        _snwprintf(Text,
                   ARRAYSIZE(Text),
                   L"TID %lu",
                   State->ThreadIds[Lane]);
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
        TextRect.top = 22;
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

    RperfTimelineDrawMarkers(Dc, &GraphRect, State, VisibleLanes);

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
        if (State->ThreadCountExact)
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
RperfTimelineCancelDrag(HWND Window,
                        RPERF_TIMELINE_STATE *State)
{
    if (State->Dragging)
        State->Range = State->DragOriginalRange;
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
            SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)State);
            return 0;

        case WM_DESTROY:
            if (State != NULL)
            {
                RperfTimelineCancelDrag(Window, State);
                HeapFree(GetProcessHeap(), 0, State);
                SetWindowLongPtrW(Window, GWLP_USERDATA, 0);
            }
            return 0;

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
                LONG X = GET_X_LPARAM(LParam);
                LONG Y = GET_Y_LPARAM(LParam);

                if (RperfTimelineGetGraphRect(Window, &GraphRect) &&
                    X >= GraphRect.left && X <= GraphRect.right &&
                    Y >= GraphRect.top && Y < GraphRect.bottom)
                {
                    State->DragAnchorUs =
                        RperfTimelinePointToTime(State, &GraphRect, X);
                    State->DragOriginalRange = State->Range;
                    State->Range.StartUs = State->DragAnchorUs;
                    State->Range.EndUs = State->DragAnchorUs;
                    State->Dragging = TRUE;
                    SetFocus(Window);
                    SetCapture(Window);
                    InvalidateRect(Window, NULL, FALSE);
                }
            }
            return 0;

        case WM_MOUSEMOVE:
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
                    InvalidateRect(Window, NULL, FALSE);
                }
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
