/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Interactive flame graph control
 */

#include "rosprofiler.h"

#include <windowsx.h>
#include <wchar.h>

#define RPERF_FLAME_BAR_HEIGHT 22
#define RPERF_FLAME_HEADER_HEIGHT 20

typedef struct _RPERF_HIT_RECT
{
    RECT Rect;
    ULONG NodeIndex;
} RPERF_HIT_RECT;

typedef struct _RPERF_FLAME_STATE
{
    const RPERF_SESSION *Session;
    ULONG ViewRoot;
    ULONG HoverNode;
    RPERF_HIT_RECT *HitRects;
    SIZE_T HitCount;
    SIZE_T HitCapacity;
    BOOL TrackingMouse;
    WCHAR Search[128];
} RPERF_FLAME_STATE;

static BOOL
RperfFlameContainsText(PCWSTR Text,
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

static BOOL
RperfFlameReserveHits(RPERF_FLAME_STATE *State,
                      SIZE_T Required)
{
    SIZE_T Capacity;
    PVOID Buffer;

    if (Required <= State->HitCapacity)
        return TRUE;

    Capacity = State->HitCapacity ? State->HitCapacity * 2 : 256;
    while (Capacity < Required)
    {
        if (Capacity > ((SIZE_T)-1) / 2)
            return FALSE;
        Capacity *= 2;
    }

    if (State->HitRects != NULL)
    {
        Buffer = HeapReAlloc(GetProcessHeap(),
                             0,
                             State->HitRects,
                             Capacity * sizeof(*State->HitRects));
    }
    else
    {
        Buffer = HeapAlloc(GetProcessHeap(),
                           0,
                           Capacity * sizeof(*State->HitRects));
    }

    if (Buffer == NULL)
        return FALSE;

    State->HitRects = Buffer;
    State->HitCapacity = Capacity;
    return TRUE;
}

static VOID
RperfFlameAddHit(RPERF_FLAME_STATE *State,
                 const RECT *Rect,
                 ULONG NodeIndex)
{
    if (!RperfFlameReserveHits(State, State->HitCount + 1))
        return;

    State->HitRects[State->HitCount].Rect = *Rect;
    State->HitRects[State->HitCount].NodeIndex = NodeIndex;
    State->HitCount++;
}

static COLORREF
RperfFlameColor(DWORD64 Address)
{
    DWORD Hash = (DWORD)(Address ^ (Address >> 32));
    BYTE Red, Green, Blue;

    Hash ^= Hash >> 16;
    Hash *= 0x7feb352d;
    Hash ^= Hash >> 15;
    Red = (BYTE)(205 + (Hash & 0x2f));
    Green = (BYTE)(80 + ((Hash >> 8) & 0x5f));
    Blue = (BYTE)(45 + ((Hash >> 16) & 0x3f));
    return RGB(Red, Green, Blue);
}

static VOID
RperfFlameDrawNode(HDC Dc,
                   const RECT *GraphRect,
                   RPERF_FLAME_STATE *State,
                   ULONG NodeIndex,
                   LONG Left,
                   LONG Right,
                   LONG Bottom)
{
    const RPERF_SESSION *Session = State->Session;
    const RPERF_NODE *Node;
    RECT Rect;
    HBRUSH Brush;
    WCHAR Text[384];
    ULONG Child;
    ULONGLONG ChildPopulation = 0;

    if (NodeIndex >= Session->NodeCount || Right <= Left ||
        Bottom <= GraphRect->top)
    {
        return;
    }

    Node = &Session->Nodes[NodeIndex];
    if (Node->Count == 0)
        return;

    Rect.left = Left;
    Rect.right = Right;
    Rect.bottom = Bottom;
    Rect.top = Bottom - RPERF_FLAME_BAR_HEIGHT;
    if (Rect.top < GraphRect->top)
        Rect.top = GraphRect->top;

    RperfFormatSymbol(Session, Node->Address, Text, ARRAYSIZE(Text));
    Brush = CreateSolidBrush(State->Search[0] != UNICODE_NULL &&
                             !RperfFlameContainsText(Text, State->Search) ?
                             RGB(220, 220, 220) :
                             RperfFlameColor(Node->Address));
    if (Brush != NULL)
    {
        FillRect(Dc, &Rect, Brush);
        DeleteObject(Brush);
    }
    FrameRect(Dc, &Rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

    if (Rect.right - Rect.left >= 4 && Rect.bottom - Rect.top >= 4)
        RperfFlameAddHit(State, &Rect, NodeIndex);

    if (Rect.right - Rect.left >= 36)
    {
        RECT TextRect = Rect;
        TextRect.left += 4;
        TextRect.right -= 3;
        SetBkMode(Dc, TRANSPARENT);
        SetTextColor(Dc, RGB(20, 20, 20));
        DrawTextW(Dc,
                  Text,
                  -1,
                  &TextRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (Rect.top <= GraphRect->top)
        return;

    Child = Node->FirstChild;
    while (Child != RPERF_INVALID_NODE)
    {
        const RPERF_NODE *ChildNode;
        LONG ChildLeft;
        LONG ChildRight;

        if (Child >= Session->NodeCount)
            break;
        ChildNode = &Session->Nodes[Child];
        ChildLeft = Left + (LONG)(((ULONGLONG)(Right - Left) *
                                  ChildPopulation) / Node->Count);
        ChildPopulation += ChildNode->Count;
        ChildRight = Left + (LONG)(((ULONGLONG)(Right - Left) *
                                   ChildPopulation) / Node->Count);
        if (ChildRight > ChildLeft)
        {
            RperfFlameDrawNode(Dc,
                               GraphRect,
                               State,
                               Child,
                               ChildLeft,
                               ChildRight,
                               Rect.top);
        }
        Child = ChildNode->NextSibling;
    }
}

static VOID
RperfFlamePaint(HWND Window,
                RPERF_FLAME_STATE *State)
{
    PAINTSTRUCT Paint;
    RECT Client, GraphRect;
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
    Bitmap = CreateCompatibleBitmap(Dc,
                                    Client.right - Client.left,
                                    Client.bottom - Client.top);
    if (MemoryDc == NULL || Bitmap == NULL)
    {
        if (MemoryDc != NULL)
            DeleteDC(MemoryDc);
        if (Bitmap != NULL)
            DeleteObject(Bitmap);
        EndPaint(Window, &Paint);
        return;
    }

    OldBitmap = SelectObject(MemoryDc, Bitmap);
    OldFont = SelectObject(MemoryDc, GetStockObject(DEFAULT_GUI_FONT));
    FillRect(MemoryDc, &Client, GetSysColorBrush(COLOR_WINDOW));
    State->HitCount = 0;

    GraphRect = Client;
    GraphRect.top += RPERF_FLAME_HEADER_HEIGHT;
    SetTextColor(MemoryDc, GetSysColor(COLOR_WINDOWTEXT));
    SetBkMode(MemoryDc, TRANSPARENT);
    DrawTextW(MemoryDc,
              L"Width is sample population. Click a frame to zoom; right-click to go back.",
              -1,
              &Client,
              DT_TOP | DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    if (State->Session == NULL || State->Session->NodeCount == 0 ||
        State->Session->Nodes[0].Count == 0)
    {
        RECT EmptyRect = GraphRect;
        DrawTextW(MemoryDc,
                  L"Record a process or open an .rperf log to view its flame graph.",
                  -1,
                  &EmptyRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else if (State->ViewRoot != 0 &&
             State->ViewRoot < State->Session->NodeCount)
    {
        RperfFlameDrawNode(MemoryDc,
                           &GraphRect,
                           State,
                           State->ViewRoot,
                           GraphRect.left,
                           GraphRect.right,
                           GraphRect.bottom);
    }
    else
    {
        const RPERF_NODE *Root = &State->Session->Nodes[0];
        ULONG Child = Root->FirstChild;
        ULONGLONG Population = 0;

        State->ViewRoot = 0;
        while (Child != RPERF_INVALID_NODE)
        {
            const RPERF_NODE *ChildNode;
            LONG Left, Right;

            if (Child >= State->Session->NodeCount)
                break;
            ChildNode = &State->Session->Nodes[Child];
            Left = GraphRect.left +
                (LONG)(((ULONGLONG)(GraphRect.right - GraphRect.left) *
                        Population) / Root->Count);
            Population += ChildNode->Count;
            Right = GraphRect.left +
                (LONG)(((ULONGLONG)(GraphRect.right - GraphRect.left) *
                        Population) / Root->Count);
            if (Right > Left)
            {
                RperfFlameDrawNode(MemoryDc,
                                   &GraphRect,
                                   State,
                                   Child,
                                   Left,
                                   Right,
                                   GraphRect.bottom);
            }
            Child = ChildNode->NextSibling;
        }
    }

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
    DeleteObject(Bitmap);
    DeleteDC(MemoryDc);
    EndPaint(Window, &Paint);
}

static ULONG
RperfFlameHitTest(const RPERF_FLAME_STATE *State,
                  POINT Point)
{
    SIZE_T Index = State->HitCount;

    while (Index-- != 0)
    {
        if (PtInRect(&State->HitRects[Index].Rect, Point))
            return State->HitRects[Index].NodeIndex;
    }
    return RPERF_INVALID_NODE;
}

static VOID
RperfFlameSetHover(HWND Window,
                   RPERF_FLAME_STATE *State,
                   ULONG NodeIndex)
{
    if (State->HoverNode == NodeIndex)
        return;

    State->HoverNode = NodeIndex;
    SendMessageW(GetAncestor(Window, GA_ROOT),
                 WM_RPERF_FLAME_HOVER,
                 NodeIndex,
                 0);
}

static LRESULT CALLBACK
RperfFlameWndProc(HWND Window,
                  UINT Message,
                  WPARAM WParam,
                  LPARAM LParam)
{
    RPERF_FLAME_STATE *State =
        (RPERF_FLAME_STATE *)GetWindowLongPtrW(Window, GWLP_USERDATA);

    switch (Message)
    {
        case WM_CREATE:
            State = HeapAlloc(GetProcessHeap(),
                              HEAP_ZERO_MEMORY,
                              sizeof(*State));
            if (State == NULL)
                return -1;
            State->ViewRoot = 0;
            State->HoverNode = RPERF_INVALID_NODE;
            SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)State);
            return 0;

        case WM_DESTROY:
            if (State != NULL)
            {
                if (State->HitRects != NULL)
                    HeapFree(GetProcessHeap(), 0, State->HitRects);
                HeapFree(GetProcessHeap(), 0, State);
                SetWindowLongPtrW(Window, GWLP_USERDATA, 0);
            }
            return 0;

        case WM_RPERF_SET_SESSION:
            if (State != NULL)
            {
                State->Session = (const RPERF_SESSION *)LParam;
                State->ViewRoot = 0;
                State->HoverNode = RPERF_INVALID_NODE;
                State->HitCount = 0;
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;

        case WM_RPERF_RESET_ZOOM:
            if (State != NULL)
            {
                State->ViewRoot = 0;
                State->HitCount = 0;
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;

        case WM_RPERF_SET_SEARCH:
            if (State != NULL)
            {
                lstrcpynW(State->Search,
                          LParam != 0 ? (PCWSTR)LParam : L"",
                          ARRAYSIZE(State->Search));
                State->HitCount = 0;
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;

        case WM_RPERF_ZOOM_ADDRESS:
            if (State != NULL && State->Session != NULL)
            {
                DWORD64 Address = (DWORD64)(ULONG_PTR)LParam;
                SIZE_T Index;
                ULONGLONG BestCount = 0;
                ULONG BestNode = RPERF_INVALID_NODE;

                for (Index = 1; Index < State->Session->NodeCount; ++Index)
                {
                    if (State->Session->Nodes[Index].Address == Address &&
                        State->Session->Nodes[Index].Count > BestCount)
                    {
                        BestCount = State->Session->Nodes[Index].Count;
                        BestNode = (ULONG)Index;
                    }
                }
                if (BestNode != RPERF_INVALID_NODE)
                    State->ViewRoot = BestNode;
                State->HitCount = 0;
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;

        case WM_PAINT:
            if (State != NULL)
                RperfFlamePaint(Window, State);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            if (State != NULL)
                State->HitCount = 0;
            InvalidateRect(Window, NULL, FALSE);
            return 0;

        case WM_MOUSEMOVE:
            if (State != NULL)
            {
                POINT Point;
                TRACKMOUSEEVENT Track;

                Point.x = GET_X_LPARAM(LParam);
                Point.y = GET_Y_LPARAM(LParam);
                RperfFlameSetHover(Window,
                                   State,
                                   RperfFlameHitTest(State, Point));
                if (!State->TrackingMouse)
                {
                    ZeroMemory(&Track, sizeof(Track));
                    Track.cbSize = sizeof(Track);
                    Track.dwFlags = TME_LEAVE;
                    Track.hwndTrack = Window;
                    if (TrackMouseEvent(&Track))
                        State->TrackingMouse = TRUE;
                }
            }
            return 0;

        case WM_MOUSELEAVE:
            if (State != NULL)
            {
                State->TrackingMouse = FALSE;
                RperfFlameSetHover(Window, State, RPERF_INVALID_NODE);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (State != NULL)
            {
                POINT Point;
                ULONG Node;
                Point.x = GET_X_LPARAM(LParam);
                Point.y = GET_Y_LPARAM(LParam);
                Node = RperfFlameHitTest(State, Point);
                if (Node != RPERF_INVALID_NODE)
                {
                    State->ViewRoot = Node;
                    State->HitCount = 0;
                    InvalidateRect(Window, NULL, FALSE);
                }
            }
            return 0;

        case WM_RBUTTONDOWN:
            if (State != NULL && State->Session != NULL &&
                State->ViewRoot != 0 &&
                State->ViewRoot < State->Session->NodeCount)
            {
                ULONG Parent = State->Session->Nodes[State->ViewRoot].Parent;
                State->ViewRoot = (Parent == RPERF_INVALID_NODE) ? 0 : Parent;
                State->HitCount = 0;
                InvalidateRect(Window, NULL, FALSE);
            }
            return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

BOOL
RperfRegisterFlameGraph(HINSTANCE Instance)
{
    WNDCLASSEXW Class;

    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.style = CS_HREDRAW | CS_VREDRAW;
    Class.lpfnWndProc = RperfFlameWndProc;
    Class.hInstance = Instance;
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    Class.lpszClassName = RPERF_FLAME_CLASS;
    return RegisterClassExW(&Class) != 0;
}

VOID
RperfUnregisterFlameGraph(HINSTANCE Instance)
{
    UnregisterClassW(RPERF_FLAME_CLASS, Instance);
}
