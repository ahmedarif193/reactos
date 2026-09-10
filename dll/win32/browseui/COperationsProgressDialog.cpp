/*
 * PROJECT:     ReactOS browseui
 * LICENSE:     GPL-3.0-or-later
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Modern shell progress dialog; public SDK contract implementation
 */
#include "precomp.h"
#include "operationsprogress.h"

#define WM_OP_REFRESH (WM_APP + 30)
#define WM_OP_CLOSE (WM_APP + 31)

#define OP_TMT_COMPOSITED 2204
#define OP_TMT_FILLCOLOR  3802

namespace
{
BOOL HighContrast()
{
    HIGHCONTRASTW contrast = {sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    return !!(contrast.dwFlags & HCF_HIGHCONTRASTON);
}

UINT Luminance(COLORREF color)
{
    return (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000;
}

COLORREF Translucent(COLORREF back, COLORREF color, int spread);

COLORREF Mix(COLORREF a, COLORREF b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    return RGB(GetRValue(a) + MulDiv(GetRValue(b) - GetRValue(a), t, 255),
               GetGValue(a) + MulDiv(GetGValue(b) - GetGValue(a), t, 255),
               GetBValue(a) + MulDiv(GetBValue(b) - GetBValue(a), t, 255));
}

COLORREF Translucent(COLORREF back, COLORREF color, int spread)
{
    int dr = GetRValue(back) - GetRValue(color), dg = GetGValue(back) - GetGValue(color);
    int db = GetBValue(back) - GetBValue(color);
    int distance = max(max(dr < 0 ? -dr : dr, dg < 0 ? -dg : dg), db < 0 ? -db : db);
    if (distance <= spread) return color;
    return Mix(back, color, spread * 255 / distance);
}

BOOL DarkTheme()
{
    DWORD value = 1, size = sizeof(value), type = 0;
    if (SHGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"AppsUseLightTheme", &type, &value, &size) == ERROR_SUCCESS && type == REG_DWORD)
        return value == 0;
    return Luminance(GetSysColor(COLOR_WINDOW)) < 128;
}

BOOL MaterialColor(PCWSTR role, COLORREF *color)
{
    if (!IsThemeActive()) return FALSE;
    WCHAR liquid[64];
    StringCchPrintfW(liquid, _countof(liquid), L"%s::Liquid", role);
    HTHEME theme = OpenThemeData(NULL, liquid);
    if (!theme) return FALSE;
    BOOL composited = FALSE;
    HRESULT hr = GetThemeBool(theme, 0, 0, OP_TMT_COMPOSITED, &composited);
    if (SUCCEEDED(hr) && composited) hr = GetThemeColor(theme, 0, 0, OP_TMT_FILLCOLOR, color);
    else hr = E_FAIL;
    CloseThemeData(theme);
    return SUCCEEDED(hr);
}

// Microsoft Fluent UI System Icons (MIT), rasterised at their design sizes.
int FluentSize(int box)
{
    static const int sizes[] = {16, 20, 24, 28, 32, 40, 48, 64, 96, 128, 256};
    int best = sizes[0];
    for (UINT i = 0; i < _countof(sizes); ++i)
    {
        if (sizes[i] <= box) best = sizes[i];
    }
    return best;
}

HICON FluentIcon(UINT id, int box)
{
    return (HICON)LoadImageW(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCEW(id),
                             IMAGE_ICON, box, box, LR_SHARED);
}

BOOL DrawFluentGlyph(HDC dc, RECT rect, UINT id, int box)
{
    int size = FluentSize(box);
    HICON icon = FluentIcon(id, size);
    if (!icon) return FALSE;
    DrawIconEx(dc, (rect.left + rect.right - size) / 2, (rect.top + rect.bottom - size) / 2,
               icon, size, size, 0, NULL, DI_NORMAL);
    return TRUE;
}
class ProgressLock
{
    CRITICAL_SECTION *m_Lock;
public:
    explicit ProgressLock(CRITICAL_SECTION *lock) : m_Lock(lock) { EnterCriticalSection(lock); }
    ~ProgressLock() { LeaveCriticalSection(m_Lock); }
};

UINT Percentage(ULONGLONG current, ULONGLONG total)
{
    if (!total) return 0;
    if (current >= total) return 100;
    return (UINT)((double)current * 100.0 / (double)total);
}

CStringW Number(ULONGLONG value)
{
    WCHAR raw[32], formatted[80];
    StringCchPrintfW(raw, _countof(raw), L"%I64u", value);
    NUMBERFMTW format = {0};
    WCHAR decimal[8], thousands[8], grouping[16];
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, decimal, _countof(decimal));
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, thousands, _countof(thousands));
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, grouping, _countof(grouping));
    format.LeadingZero = 1;
    format.Grouping = grouping[0] >= L'0' && grouping[0] <= L'9' ? grouping[0] - L'0' : 3;
    format.lpDecimalSep = decimal;
    format.lpThousandSep = thousands;
    if (GetNumberFormatW(LOCALE_USER_DEFAULT, 0, raw, &format, formatted, _countof(formatted))) return formatted;
    return raw;
}

CStringW LinkText(CStringW value, PCWSTR id)
{
    value.Replace(L"&", L"&amp;");
    value.Replace(L"<", L"&lt;");
    value.Replace(L">", L"&gt;");
    CStringW result;
    result.Format(L"<a id=\"%s\">%s</a>", id, (PCWSTR)value);
    return result;
}

int TextWidth(HDC dc, const CStringW &text)
{
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, text.GetLength(), &size);
    return size.cx;
}

CStringW FitLabel(HDC dc, const CStringW &text, int width)
{
    if (TextWidth(dc, text) <= width) return text;
    int count = 0;
    SIZE size;
    GetTextExtentExPointW(dc, text, text.GetLength(), max(0, width - TextWidth(dc, L"...")), &count, NULL, &size);
    return text.Left(count) + L"...";
}

CStringW ItemName(IShellItem *item, SIGDN kind)
{
    CStringW result;
    PWSTR value = NULL;
    if (item && SUCCEEDED(item->GetDisplayName(kind, &value))) result = value;
    CoTaskMemFree(value);
    return result;
}

void Fill(HDC dc, const RECT &rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
}

COperationsProgressDialog::COperationsProgressDialog() :
    m_Window(NULL), m_Owner(NULL), m_DisabledOwner(NULL), m_Thread(NULL), m_Ready(NULL), m_CreateResult(E_FAIL),
    m_Flags(0), m_Modern(FALSE), m_ModernStarted(FALSE), m_Expanded(FALSE), m_TimerPaused(FALSE), m_Cancelled(FALSE),
    m_Theming(FALSE),
    m_Mode(PDM_DEFAULT), m_Status(PDOPS_ERRORS), m_Action(SPACTION_NONE),
    m_Points(0), m_TotalPoints(0), m_Bytes(0), m_TotalBytes(0), m_Items(0), m_TotalItems(0),
    m_Start(0), m_PauseStart(0), m_PausedTime(0), m_SampleTime(0), m_SampleBytes(0),
    m_Speed(0), m_HistoryCount(0), m_HistoryNext(0), m_Dpi(96), m_Font(NULL), m_LargeFont(NULL),
    m_Brush(NULL), m_Theme(NULL)
{
    InitializeCriticalSection(&m_Lock);
    InitializeCriticalSection(&m_Lifecycle);
    ZeroMemory(m_History, sizeof(m_History));
    ZeroMemory(&m_Palette, sizeof(m_Palette));
    ApplyTheme();
}

COperationsProgressDialog::~COperationsProgressDialog()
{
    if (m_Thread) CloseHandle(m_Thread);
    if (m_Font) DeleteObject(m_Font);
    if (m_LargeFont) DeleteObject(m_LargeFont);
    if (m_Brush) DeleteObject(m_Brush);
    if (m_Theme) CloseThemeData(m_Theme);
    DeleteCriticalSection(&m_Lifecycle);
    DeleteCriticalSection(&m_Lock);
}

void COperationsProgressDialog::ApplyTheme()
{
    if (m_Theming) return;
    m_Theming = TRUE;
    BOOL contrast = HighContrast();
    BOOL dark = !contrast && DarkTheme();
    PCWSTR role = dark ? L"FlyoutDark" : L"FlyoutLight";
    COLORREF material = 0;
    BOOL blur = !contrast && MaterialColor(role, &material);

    m_Palette.Dark = dark;
    m_Palette.Material = blur;
    m_Palette.Back = contrast ? GetSysColor(COLOR_WINDOW)
                              : blur ? material : dark ? RGB(43, 43, 43) : RGB(255, 255, 255);
    m_Palette.Text = contrast ? GetSysColor(COLOR_WINDOWTEXT)
                              : dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
    m_Palette.Dim = contrast ? GetSysColor(COLOR_GRAYTEXT) : Mix(m_Palette.Text, m_Palette.Back, 100);
    m_Palette.Line = contrast ? GetSysColor(COLOR_WINDOWTEXT) : Mix(m_Palette.Back, m_Palette.Text, dark ? 46 : 26);
    m_Palette.Hot = contrast ? GetSysColor(COLOR_HIGHLIGHT) : Mix(m_Palette.Back, m_Palette.Text, dark ? 30 : 20);
    m_Palette.Press = contrast ? GetSysColor(COLOR_HIGHLIGHT) : Mix(m_Palette.Back, m_Palette.Text, dark ? 58 : 40);
    m_Palette.Track = contrast ? GetSysColor(COLOR_WINDOW) : Mix(m_Palette.Back, m_Palette.Text, dark ? 34 : 26);
    m_Palette.Border = contrast ? GetSysColor(COLOR_WINDOWTEXT) : Mix(m_Palette.Back, m_Palette.Text, dark ? 66 : 74);

    if (m_Brush) DeleteObject(m_Brush);
    m_Brush = CreateSolidBrush(m_Palette.Back);
    if (m_Theme)
    {
        CloseThemeData(m_Theme);
        m_Theme = NULL;
    }
    if (m_Window)
    {
        SetWindowTheme(m_Window, blur ? role : NULL, NULL);
        if (blur) m_Theme = OpenThemeData(m_Window, role);
        InvalidateRect(m_Window, NULL, TRUE);
    }
    m_Theming = FALSE;
}

UINT COperationsProgressDialog::Glyph(UINT id) const
{
    if (!m_Palette.Dark) return id;
    switch (id)
    {
        case IDI_OP_PAUSE: return IDI_OP_PAUSE_LIGHT;
        case IDI_OP_RESUME: return IDI_OP_RESUME_LIGHT;
        case IDI_OP_CANCEL: return IDI_OP_CANCEL_LIGHT;
        case IDI_OP_MORE: return IDI_OP_MORE_LIGHT;
        case IDI_OP_FEWER: return IDI_OP_FEWER_LIGHT;
        case IDI_OP_COPY: return IDI_OP_COPY_LIGHT;
    }
    return id;
}

CStringW COperationsProgressDialog::Text(UINT id) const
{
    CStringW value;
    value.LoadString(_AtlBaseModule.GetResourceInstance(), id);
    return value;
}

void COperationsProgressDialog::Notify()
{
    if (m_Window) PostMessageW(m_Window, WM_OP_REFRESH, 0, 0);
}

ULONGLONG COperationsProgressDialog::Elapsed() const
{
    if (!m_Start) return 0;
    return (m_TimerPaused ? m_PauseStart : GetTickCount64()) - m_Start - m_PausedTime;
}

ULONGLONG COperationsProgressDialog::Remaining() const
{
    if (!m_Points || !m_TotalPoints || m_Points >= m_TotalPoints) return 0;
    double estimate = (double)Elapsed() * (double)(m_TotalPoints - m_Points) / (double)m_Points;
    return estimate >= (double)MAXULONGLONG ? MAXULONGLONG : (ULONGLONG)estimate;
}

void COperationsProgressDialog::Pause(BOOL paused)
{
    if (paused == m_TimerPaused) return;
    ULONGLONG now = GetTickCount64();
    if (paused) m_PauseStart = now;
    else
    {
        m_PausedTime += now - m_PauseStart;
        m_SampleTime = now;
        m_SampleBytes = m_Bytes;
    }
    m_TimerPaused = paused;
}

HRESULT COperationsProgressDialog::Start(HWND owner, DWORD flags, BOOL modern)
{
    ProgressLock lifecycle(&m_Lifecycle);
    {
        ProgressLock lock(&m_Lock);
        if (m_ModernStarted) return E_UNEXPECTED;
        if (m_Window) return S_OK;
        m_Owner = owner;
        m_Flags = flags;
        m_Modern = modern;
        m_Cancelled = FALSE;
        m_Status = PDOPS_RUNNING;
        m_Mode = (flags & PROGDLG_MARQUEEPROGRESS) ? PDM_INDETERMINATE : PDM_RUN;
        m_Start = m_SampleTime = GetTickCount64();
        m_PausedTime = 0;
        m_TimerPaused = FALSE;
        m_SampleBytes = m_Bytes;
        m_HistoryCount = m_HistoryNext = 0;
        m_Speed = 0;
        m_CreateResult = E_FAIL;
    }
    if (m_Thread)
    {
        WaitForSingleObject(m_Thread, INFINITE);
        CloseHandle(m_Thread);
        m_Thread = NULL;
    }
    m_Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!m_Ready) return HRESULT_FROM_WIN32(GetLastError());
    AddRef(); // The window thread owns a reference until all its messages have drained.
    m_Thread = CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
    if (!m_Thread)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(m_Ready);
        m_Ready = NULL;
        Release();
        return hr;
    }
    WaitForSingleObject(m_Ready, INFINITE);
    CloseHandle(m_Ready);
    m_Ready = NULL;
    {
        ProgressLock lock(&m_Lock);
        if (modern && SUCCEEDED(m_CreateResult)) m_ModernStarted = TRUE;
    }
    // Win11's legacy IProgressDialog leaves its owner enabled.
    if (modern && SUCCEEDED(m_CreateResult) && (flags & PROGDLG_MODAL) && owner && IsWindowEnabled(owner))
    {
        m_DisabledOwner = owner;
        EnableWindow(owner, FALSE);
    }
    return m_CreateResult;
}

STDMETHODIMP COperationsProgressDialog::StartProgressDialog(HWND owner, OPPROGDLGF flags)
{
    return Start(owner, flags, TRUE);
}

STDMETHODIMP COperationsProgressDialog::StartProgressDialog(HWND owner, IUnknown *modeless, DWORD flags, LPCVOID reserved)
{
    return Start(owner, flags, FALSE);
}

STDMETHODIMP COperationsProgressDialog::StopProgressDialog()
{
    ProgressLock lifecycle(&m_Lifecycle);
    {
        ProgressLock lock(&m_Lock);
        if (m_Window) PostMessageW(m_Window, WM_OP_CLOSE, 0, 0);
    }
    if (m_Thread)
    {
        // Dispatch COM calls while waiting; the UI may be using the shell taskbar proxy.
        DWORD index;
        HRESULT hr = CoWaitForMultipleHandles(0, INFINITE, 1, &m_Thread, &index);
        if (FAILED(hr)) WaitForSingleObject(m_Thread, INFINITE);
        CloseHandle(m_Thread);
        m_Thread = NULL;
    }
    if (m_DisabledOwner)
    {
        EnableWindow(m_DisabledOwner, TRUE);
        m_DisabledOwner = NULL;
    }
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::SetOperation(SPACTION action)
{
    ProgressLock lock(&m_Lock);
    if (!m_ModernStarted) return E_UNEXPECTED;
    m_Action = action;
    Notify();
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::SetMode(PDMODE mode)
{
    ProgressLock lock(&m_Lock);
    if (!m_ModernStarted) return E_UNEXPECTED;
    m_Mode = mode;
    if (!m_Cancelled && m_Status != PDOPS_PAUSED) m_Status = (mode & PDM_ERRORSBLOCKING) ? PDOPS_ERRORS : PDOPS_RUNNING;
    Notify();
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::UpdateProgress(ULONGLONG points, ULONGLONG totalPoints, ULONGLONG bytes, ULONGLONG totalBytes, ULONGLONG items, ULONGLONG totalItems)
{
    ProgressLock lock(&m_Lock);
    if (!m_ModernStarted) return E_UNEXPECTED;
    m_Points = points;
    m_TotalPoints = totalPoints;
    m_Bytes = bytes;
    m_TotalBytes = totalBytes;
    m_Items = items;
    m_TotalItems = totalItems;
    // The timer coalesces frequent CopyFileEx notifications into UI updates.
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::UpdateLocations(IShellItem *source, IShellItem *target, IShellItem *item)
{
    // Shell item calls belong on the caller's COM apartment, outside our state lock.
    CStringW sourceName = ItemName(source, SIGDN_NORMALDISPLAY), targetName = ItemName(target, SIGDN_NORMALDISPLAY);
    CStringW itemName = ItemName(item, SIGDN_NORMALDISPLAY);
    CStringW sourcePath = ItemName(source, SIGDN_FILESYSPATH), targetPath = ItemName(target, SIGDN_FILESYSPATH);
    ProgressLock lock(&m_Lock);
    if (!m_ModernStarted) return E_UNEXPECTED;
    if (source) { m_Source = sourceName; m_SourcePath = sourcePath; }
    if (target) { m_Target = targetName; m_TargetPath = targetPath; }
    if (item) m_Item = itemName;
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::ResetTimer()
{
    ProgressLock lock(&m_Lock);
    m_Start = m_SampleTime = GetTickCount64();
    m_PauseStart = m_Start;
    m_PausedTime = 0;
    m_SampleBytes = m_Bytes;
    m_Speed = 0;
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::PauseTimer()
{
    ProgressLock lock(&m_Lock);
    Pause(TRUE);
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::ResumeTimer()
{
    ProgressLock lock(&m_Lock);
    Pause(FALSE);
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::GetMilliseconds(ULONGLONG *elapsed, ULONGLONG *remaining)
{
    if (!elapsed || !remaining) return E_POINTER;
    ProgressLock lock(&m_Lock);
    *elapsed = Elapsed();
    *remaining = Remaining();
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::GetOperationStatus(PDOPSTATUS *status)
{
    if (!status) return E_POINTER;
    ProgressLock lock(&m_Lock);
    *status = m_Status;
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::SetTitle(LPCWSTR title)
{
    ProgressLock lock(&m_Lock);
    if (m_ModernStarted) return E_UNEXPECTED;
    m_Title = title ? title : L"";
    Notify();
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::SetAnimation(HINSTANCE instance, UINT resource)
{
    return S_OK; // The modern presentation uses the throughput graph instead of an AVI.
}

BOOL STDMETHODCALLTYPE COperationsProgressDialog::HasUserCancelled()
{
    ProgressLock lock(&m_Lock);
    return m_Cancelled;
}

STDMETHODIMP COperationsProgressDialog::SetProgress(DWORD completed, DWORD total)
{
    return SetProgress64(completed, total);
}

STDMETHODIMP COperationsProgressDialog::SetProgress64(ULONGLONG completed, ULONGLONG total)
{
    ProgressLock lock(&m_Lock);
    if (m_ModernStarted) return E_UNEXPECTED;
    m_Points = m_Bytes = completed;
    m_TotalPoints = m_TotalBytes = total;
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::SetLine(DWORD line, LPCWSTR text, BOOL path, LPCVOID reserved)
{
    ProgressLock lock(&m_Lock);
    if (m_ModernStarted) return E_UNEXPECTED;
    if (!line || line > 3) return E_INVALIDARG;
    m_Lines[line - 1] = text ? text : L"";
    Notify();
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::SetCancelMsg(LPCWSTR text, LPCVOID reserved)
{
    ProgressLock lock(&m_Lock);
    if (m_ModernStarted) return E_UNEXPECTED;
    m_CancelText = text ? text : L"";
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::Timer(DWORD action, LPCVOID reserved)
{
    if (action == PDTIMER_RESET) return ResetTimer();
    if (action == PDTIMER_PAUSE) return PauseTimer();
    if (action == PDTIMER_RESUME) return ResumeTimer();
    return S_OK;
}

STDMETHODIMP COperationsProgressDialog::GetWindow(HWND *window)
{
    if (!window) return E_POINTER;
    ProgressLock lock(&m_Lock);
    *window = m_Window;
    return m_Window ? S_OK : E_FAIL;
}

STDMETHODIMP COperationsProgressDialog::ContextSensitiveHelp(BOOL enter)
{
    return E_NOTIMPL;
}

void COperationsProgressDialog::Layout()
{
    const int height = m_Expanded ? 320 : 170;
    RECT bounds = {0, 0, Scale(560), Scale(height)};
    AdjustWindowRectEx(&bounds, GetWindowLongW(m_Window, GWL_STYLE), FALSE, GetWindowLongW(m_Window, GWL_EXSTYLE));
    SetWindowPos(m_Window, NULL, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    struct Placement { int id, x, y, width, height; } controls[] =
    {
        {IDC_OP_LOCATION, 32, 18, 496, 23},
        {IDC_OP_PERCENT, 32, 43, 370, 29},
        {IDC_OP_PAUSE, 438, 40, 40, 32},
        {IDCANCEL, 496, 40, 32, 32},
        {IDC_OP_GRAPH, 32, 78, 496, m_Expanded ? 100 : 20},
        {IDC_OP_NAME, 32, 190, 496, 21},
        {IDC_OP_TIME, 32, 211, 496, 21},
        {IDC_OP_ITEMS, 32, 232, 496, 21},
        {IDC_OP_DETAILS, 24, height - 44, 240, 36},
    };
    for (UINT i = 0; i < _countof(controls); ++i)
    {
        Placement &p = controls[i];
        MoveWindow(GetDlgItem(m_Window, p.id), Scale(p.x), Scale(p.y), Scale(p.width), Scale(p.height), TRUE);
    }
    ShowWindow(GetDlgItem(m_Window, IDC_OP_PAUSE), (m_Modern && (m_Flags & OPPROGDLG_ENABLEPAUSE)) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(m_Window, IDCANCEL), (m_Flags & PROGDLG_NOCANCEL) ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(m_Window, IDC_OP_GRAPH), (m_Flags & PROGDLG_NOPROGRESSBAR) ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(m_Window, IDC_OP_LOCATION), (m_Flags & OPPROGDLG_DONTDISPLAYLOCATIONS) ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(m_Window, IDC_OP_NAME), m_Expanded ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(m_Window, IDC_OP_TIME), (m_Expanded && !(m_Flags & PROGDLG_NOTIME)) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(m_Window, IDC_OP_ITEMS), m_Expanded ? SW_SHOW : SW_HIDE);
    SetDlgItemTextW(m_Window, IDC_OP_DETAILS, Text(m_Expanded ? IDS_OP_FEWER : IDS_OP_MORE));
    InvalidateRect(m_Window, NULL, TRUE);
}

void COperationsProgressDialog::Refresh()
{
    CStringW percent, line, caption;
    UINT progress = Percentage(m_Points, m_TotalPoints);
    percent.Format(Text(IDS_OP_PERCENT), progress);
    caption = percent;
    if (m_Cancelled) caption = Text(IDS_OP_CANCELLING);
    else if (m_Status == PDOPS_PAUSED) caption = Text(IDS_OP_PAUSED);
    else if (m_Mode & PDM_ERRORSBLOCKING) caption = Text(IDS_OP_ERRORS);
    else if (m_Mode & (PDM_PREFLIGHT | PDM_INDETERMINATE)) caption = Text(IDS_OP_CALCULATING);
    SetWindowTextW(m_Window, m_Modern || m_Title.IsEmpty() ? (PCWSTR)caption : (PCWSTR)m_Title);
    SetDlgItemTextW(m_Window, IDC_OP_PERCENT, caption);
    if (m_Modern)
    {
        CStringW count = Number(m_TotalItems);
        CStringW action = Text(IDS_OP_ACTION + (m_Action <= SPACTION_COPY_MOVING ? m_Action : SPACTION_NONE));
        BOOL source = !(m_Flags & OPPROGDLG_DONTDISPLAYSOURCEPATH) && !m_Source.IsEmpty();
        BOOL target = !(m_Flags & OPPROGDLG_DONTDISPLAYDESTPATH) && !m_Target.IsEmpty();
        if (source) line.Format(Text(IDS_OP_FROM), (PCWSTR)action, (PCWSTR)count, L"");
        else if (target) line.Format(Text(IDS_OP_TO), (PCWSTR)action, (PCWSTR)count, L"");
        else line.Format(Text(IDS_OP_COUNT), (PCWSTR)action, (PCWSTR)count);
        LayoutLocations(line, source, target);
        line.Format(Text(IDS_OP_NAME), (PCWSTR)m_Item);
        SetDlgItemTextW(m_Window, IDC_OP_NAME, line);
        WCHAR size[80];
        ULONGLONG bytes = m_TotalBytes > m_Bytes ? m_TotalBytes - m_Bytes : 0;
        StrFormatByteSizeW((LONGLONG)min(bytes, (ULONGLONG)MAXLONGLONG), size, _countof(size));
        line.Format(Text(IDS_OP_ITEMS), (PCWSTR)Number(m_TotalItems > m_Items ? m_TotalItems - m_Items : 0), size);
        SetDlgItemTextW(m_Window, IDC_OP_ITEMS, line);
    }
    else
    {
        LayoutLocations(m_Lines[0], FALSE, FALSE);
        SetDlgItemTextW(m_Window, IDC_OP_NAME, m_Lines[1]);
        SetDlgItemTextW(m_Window, IDC_OP_ITEMS, m_Lines[2]);
    }
    ULONGLONG remaining = Remaining();
    WCHAR time[100];
    CStringW timeText = Text(IDS_OP_CALCULATING);
    if (m_Points && !(m_Mode & (PDM_PREFLIGHT | PDM_INDETERMINATE)) && (!(m_Flags & OPPROGDLG_NOMULTIDAYESTIMATES) || remaining < 86400000))
    {
        StrFromTimeIntervalW(time, _countof(time), (DWORD)min(remaining, (ULONGLONG)MAXDWORD), 2);
        timeText = time;
    }
    line.Format(Text(IDS_OP_TIME), (PCWSTR)timeText);
    SetDlgItemTextW(m_Window, IDC_OP_TIME, line);
    if (m_Cancelled && !m_CancelText.IsEmpty()) SetDlgItemTextW(m_Window, IDC_OP_NAME, m_CancelText);
    SetDlgItemTextW(m_Window, IDC_OP_PAUSE, Text(m_Status == PDOPS_PAUSED ? IDS_OP_RESUME : IDS_OP_PAUSE));
    SetDlgItemTextW(m_Window, IDCANCEL, Text((m_Flags & OPPROGDLG_ALLOWUNDO) && !m_Cancelled ? IDS_OP_UNDO : IDS_OP_CANCEL));
    EnableWindow(GetDlgItem(m_Window, IDC_OP_PAUSE), !m_Cancelled);
    EnableWindow(GetDlgItem(m_Window, IDCANCEL), !m_Cancelled || m_Status == PDOPS_CANCELLED);
    InvalidateRect(GetDlgItem(m_Window, IDC_OP_GRAPH), NULL, FALSE);
}

void COperationsProgressDialog::LayoutLocations(const CStringW &prefix, BOOL source, BOOL target)
{
    BOOL visible = !(m_Flags & OPPROGDLG_DONTDISPLAYLOCATIONS);
    HDC dc = GetDC(m_Window);
    HGDIOBJ previous = SelectObject(dc, m_Font);
    int width = Scale(496), x = Scale(32), y = Scale(18), height = Scale(23);
    int prefixWidth = source || target ? min(width, TextWidth(dc, prefix)) : width;
    CStringW connector = source && target ? Text(IDS_OP_CONNECT) : L"";
    int connectorWidth = min(width - prefixWidth, TextWidth(dc, connector));
    int available = max(0, width - prefixWidth - connectorWidth);
    int sourceWidth = source ? min(TextWidth(dc, m_Source) + Scale(4), target ? available / 2 : available) : 0;
    int targetWidth = target ? available - sourceWidth : 0;
    SetDlgItemTextW(m_Window, IDC_OP_LOCATION, prefix);
    SetDlgItemTextW(m_Window, IDC_OP_SOURCE, LinkText(FitLabel(dc, m_Source, max(0, sourceWidth - Scale(4))), L"source"));
    SetDlgItemTextW(m_Window, IDC_OP_TO, connector);
    SetDlgItemTextW(m_Window, IDC_OP_TARGET, LinkText(FitLabel(dc, m_Target, max(0, targetWidth - Scale(4))), L"target"));
    int ids[] = {IDC_OP_LOCATION, IDC_OP_SOURCE, IDC_OP_TO, IDC_OP_TARGET};
    int widths[] = {prefixWidth, sourceWidth, connectorWidth, targetWidth};
    BOOL show[] = {TRUE, source, source && target, target};
    for (UINT i = 0; i < _countof(ids); ++i)
    {
        HWND control = GetDlgItem(m_Window, ids[i]);
        MoveWindow(control, x, y, widths[i], height, TRUE);
        ShowWindow(control, visible && show[i] && widths[i] > 0 ? SW_SHOW : SW_HIDE);
        x += widths[i];
    }
    SelectObject(dc, previous);
    ReleaseDC(m_Window, dc);
}

void COperationsProgressDialog::PaintGraph(HDC dc, RECT rect)
{
    BOOL highContrast = HighContrast();
    COLORREF base = highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(6, 176, 37);
    if (!highContrast && m_Status == PDOPS_PAUSED) base = RGB(234, 181, 0);
    if (!highContrast && (m_Mode & PDM_ERRORSBLOCKING)) base = RGB(210, 35, 35);
    COLORREF ink = m_Palette.Material ? Translucent(m_Palette.Back, base, 80) : base;
    COLORREF pale = highContrast ? m_Palette.Back
                                 : m_Palette.Material ? Translucent(m_Palette.Back, base, 28)
                                                      : Mix(m_Palette.Back, base, m_Palette.Dark ? 70 : 130);
    Fill(dc, rect, m_Palette.Back);
    HBRUSH border = CreateSolidBrush(m_Palette.Border);
    FrameRect(dc, &rect, border);
    DeleteObject(border);
    InflateRect(&rect, -1, -1);
    RECT filled = rect;
    filled.right = rect.left + MulDiv(rect.right - rect.left, Percentage(m_Points, m_TotalPoints), 100);
    if (m_Mode & (PDM_PREFLIGHT | PDM_INDETERMINATE))
    {
        int width = rect.right - rect.left;
        filled.left = rect.left + (GetTickCount() / 12) % max(width, 1);
        filled.right = min(rect.right, filled.left + width / 5);
        Fill(dc, filled, ink);
        return;
    }
    if (!m_Expanded)
    {
        Fill(dc, rect, m_Palette.Track);
        Fill(dc, filled, ink);
        return;
    }
    Fill(dc, rect, pale);
    HPEN grid = CreatePen(PS_SOLID, 1, highContrast ? GetSysColor(COLOR_GRAYTEXT) : Mix(pale, ink, 90));
    HGDIOBJ oldPen = SelectObject(dc, grid);
    for (int x = rect.left + Scale(50); x < rect.right; x += Scale(50))
    {
        MoveToEx(dc, x, rect.top, NULL);
        LineTo(dc, x, rect.bottom);
    }
    for (int y = rect.top + Scale(20); y < rect.bottom; y += Scale(20))
    {
        MoveToEx(dc, rect.left, y, NULL);
        LineTo(dc, rect.right, y);
    }
    if (m_HistoryCount)
    {
        double maximum = 1;
        for (UINT i = 0; i < m_HistoryCount; ++i) maximum = max(maximum, m_History[i]);
        POINT samples[_countof(m_History)], curve[3 * (_countof(m_History) - 1)];
        int height = rect.bottom - rect.top;
        int width = rect.right - rect.left;
        UINT count = m_HistoryCount;
        for (UINT i = 0; i < count; ++i)
        {
            UINT index = (m_HistoryNext + _countof(m_History) - count + i) % _countof(m_History);
            samples[i].x = count > 1 ? rect.left + MulDiv(width, i, count - 1) : rect.right;
            samples[i].y = rect.bottom - (int)(m_History[index] / maximum * height * 0.82);
        }
        for (UINT i = 0; i + 1 < count; ++i)
        {
            const POINT &previous = samples[i ? i - 1 : 0];
            const POINT &next = samples[min(i + 2, count - 1)];
            curve[3 * i].x = samples[i].x + (samples[i + 1].x - previous.x) / 6;
            curve[3 * i].y = samples[i].y + (samples[i + 1].y - previous.y) / 6;
            curve[3 * i + 1].x = samples[i + 1].x - (next.x - samples[i].x) / 6;
            curve[3 * i + 1].y = samples[i + 1].y - (next.y - samples[i].y) / 6;
            curve[3 * i + 2] = samples[i + 1];
            for (int k = 0; k < 2; ++k)
            {
                LONG &y = curve[3 * i + k].y;
                y = max((LONG)rect.top, min((LONG)rect.bottom, y));
            }
        }
        HBRUSH brush = CreateSolidBrush(ink);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        BeginPath(dc);
        MoveToEx(dc, rect.left, rect.bottom, NULL);
        LineTo(dc, rect.left, samples[0].y);
        if (count > 1) PolyBezierTo(dc, curve, 3 * (count - 1));
        LineTo(dc, rect.right, samples[count - 1].y);
        LineTo(dc, rect.right, rect.bottom);
        CloseFigure(dc);
        EndPath(dc);
        FillPath(dc);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    }
    SelectObject(dc, oldPen);
    DeleteObject(grid);
    WCHAR size[80];
    StrFormatByteSizeW(m_Speed >= (double)MAXLONGLONG ? MAXLONGLONG : (LONGLONG)m_Speed, size, _countof(size));
    CStringW speed;
    speed.Format(Text(IDS_OP_SPEED), size);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, m_Palette.Text);
    HGDIOBJ font = SelectObject(dc, m_Font);
    rect.right -= Scale(8);
    rect.top = rect.bottom - Scale(31);
    DrawTextW(dc, speed, -1, &rect, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, font);
}

void COperationsProgressDialog::PaintButton(DRAWITEMSTRUCT *draw)
{
    HDC dc = draw->hDC;
    RECT rect = draw->rcItem;
    COLORREF color = IsWindowEnabled(draw->hwndItem) ? m_Palette.Text : m_Palette.Dim;
    Fill(dc, rect, (draw->itemState & ODS_SELECTED) ? m_Palette.Press : m_Palette.Back);
    int cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2, radius = Scale(5);
    BOOL glyphs = !HighContrast() && IsWindowEnabled(draw->hwndItem);
    HPEN pen = CreatePen(PS_SOLID, max(1, Scale(2)), color);
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (draw->CtlID == IDC_OP_DETAILS)
    {
        cx = rect.left + Scale(20);
        RECT glyph = {cx - Scale(10), cy - Scale(10), cx + Scale(10), cy + Scale(10)};
        if (!glyphs || !DrawFluentGlyph(dc, glyph, Glyph(m_Expanded ? IDI_OP_FEWER : IDI_OP_MORE), Scale(20)))
        {
            Ellipse(dc, cx - Scale(10), cy - Scale(10), cx + Scale(10), cy + Scale(10));
            int direction = m_Expanded ? -1 : 1;
            MoveToEx(dc, cx - radius, cy - direction * Scale(2), NULL);
            LineTo(dc, cx, cy + direction * Scale(3));
            LineTo(dc, cx + radius, cy - direction * Scale(2));
        }
        rect.left += Scale(38);
        HGDIOBJ oldFont = SelectObject(dc, m_Font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextW(dc, Text(m_Expanded ? IDS_OP_FEWER : IDS_OP_MORE), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }
    else if (draw->CtlID == IDC_OP_PAUSE)
    {
        if (glyphs && DrawFluentGlyph(dc, rect, Glyph(m_Status == PDOPS_PAUSED ? IDI_OP_RESUME : IDI_OP_PAUSE), Scale(16)))
        {
        }
        else if (m_Status == PDOPS_PAUSED)
        {
            POINT triangle[] = {{cx - radius, cy - radius - 1}, {cx + radius, cy}, {cx - radius, cy + radius + 1}};
            HBRUSH brush = CreateSolidBrush(color);
            SelectObject(dc, brush);
            Polygon(dc, triangle, _countof(triangle));
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        }
        else
        {
            MoveToEx(dc, cx - Scale(3), cy - radius, NULL);
            LineTo(dc, cx - Scale(3), cy + radius);
            MoveToEx(dc, cx + Scale(3), cy - radius, NULL);
            LineTo(dc, cx + Scale(3), cy + radius);
        }
    }
    else if (!glyphs || !DrawFluentGlyph(dc, rect, Glyph(IDI_OP_CANCEL), Scale(16)))
    {
        MoveToEx(dc, cx - radius, cy - radius, NULL);
        LineTo(dc, cx + radius, cy + radius);
        MoveToEx(dc, cx + radius, cy - radius, NULL);
        LineTo(dc, cx - radius, cy + radius);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT)) DrawFocusRect(dc, &draw->rcItem);
}

DWORD WINAPI COperationsProgressDialog::ThreadProc(void *parameter)
{
    COperationsProgressDialog *self = static_cast<COperationsProgressDialog *>(parameter);
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    // An arbitrary COM client need not opt into common controls v6. Activate
    // our DLL manifest on the new thread before resolving/registering SysLink.
    ACTCTXW activation = {sizeof(activation)};
    activation.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
    activation.hModule = _AtlBaseModule.GetResourceInstance();
    activation.lpResourceName = MAKEINTRESOURCEW(ISOLATIONAWARE_MANIFEST_RESOURCE_ID);
    HANDLE context = CreateActCtxW(&activation);
    ULONG_PTR cookie = 0;
    BOOL activated = context != INVALID_HANDLE_VALUE && ActivateActCtx(context, &cookie);
    HMODULE commonControls = LoadLibraryW(L"comctl32.dll");
    typedef BOOL (WINAPI *INITIALIZE_CONTROLS)(const INITCOMMONCONTROLSEX *);
    INITIALIZE_CONTROLS initialize = commonControls ? reinterpret_cast<INITIALIZE_CONTROLS>(GetProcAddress(commonControls, "InitCommonControlsEx")) : NULL;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LINK_CLASS | ICC_STANDARD_CLASSES};
    if (initialize) initialize(&controls);
    HWND window = CreateDialogParamW(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCEW(IDD_OPERATIONS_PROGRESS), self->m_Owner, DialogProc, (LPARAM)self);
    self->m_CreateResult = window ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    SetEvent(self->m_Ready); // Signal failure too: the caller must never wait forever.
    if (window)
    {
        self->m_Taskbar.CoCreateInstance(CLSID_TaskbarList, IID_ITaskbarList3, NULL, CLSCTX_INPROC_SERVER);
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0)
        {
            if (!IsDialogMessageW(window, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (self->m_Taskbar) self->m_Taskbar->SetProgressState(window, TBPF_NOPROGRESS);
        self->m_Taskbar.Release();
    }
    if (commonControls) FreeLibrary(commonControls);
    if (activated) DeactivateActCtx(0, cookie);
    if (context != INVALID_HANDLE_VALUE) ReleaseActCtx(context);
    if (SUCCEEDED(com)) CoUninitialize();
    self->Release();
    return 0;
}

INT_PTR CALLBACK COperationsProgressDialog::DialogProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    COperationsProgressDialog *self = reinterpret_cast<COperationsProgressDialog *>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        self = reinterpret_cast<COperationsProgressDialog *>(lparam);
        SetWindowLongPtrW(window, DWLP_USER, (LONG_PTR)self);
        ProgressLock lock(&self->m_Lock);
        self->m_Window = window;
        self->ApplyTheme();
        HDC dc = GetDC(window);
        self->m_Dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(window, dc);
        if (self->m_Font) DeleteObject(self->m_Font);
        if (self->m_LargeFont) DeleteObject(self->m_LargeFont);
        self->m_Font = CreateFontW(-self->Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        self->m_LargeFont = CreateFontW(-self->Scale(20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        for (HWND child = ::GetWindow(window, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, (WPARAM)self->m_Font, FALSE);
        SendDlgItemMessageW(window, IDC_OP_PERCENT, WM_SETFONT, (WPARAM)self->m_LargeFont, FALSE);
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)FluentIcon(self->Glyph(IDI_OP_COPY), GetSystemMetrics(SM_CXSMICON)));
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)FluentIcon(self->Glyph(IDI_OP_COPY), GetSystemMetrics(SM_CXICON)));
        if (self->m_Flags & PROGDLG_NOMINIMIZE) SetWindowLongW(window, GWL_STYLE, GetWindowLongW(window, GWL_STYLE) & ~WS_MINIMIZEBOX);
        if (self->m_Flags & PROGDLG_NOCANCEL) EnableMenuItem(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        self->Layout();
        self->Refresh();
        SetTimer(window, 1, 100, NULL);
        return TRUE;
    }
    if (!self) return FALSE;
    if (message == WM_OP_CLOSE)
    {
        DestroyWindow(window);
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        ProgressLock lock(&self->m_Lock);
        self->m_Window = NULL;
        KillTimer(window, 1);
        PostQuitMessage(0);
        return TRUE;
    }
    if (message == WM_TIMER || message == WM_OP_REFRESH)
    {
        TBPFLAG state;
        ULONGLONG points, total;
        {
            ProgressLock lock(&self->m_Lock);
            ULONGLONG now = GetTickCount64(), interval = now - self->m_SampleTime;
            BOOL running = !self->m_TimerPaused && !self->m_Cancelled &&
                           self->m_Status != PDOPS_PAUSED &&
                           !(self->m_Mode & (PDM_PREFLIGHT | PDM_ERRORSBLOCKING));
            if (!running)
            {
                self->m_SampleTime = now;
                self->m_SampleBytes = self->m_Bytes;
            }
            else if (interval >= 250)
            {
                double rate = self->m_Bytes > self->m_SampleBytes ? (double)(self->m_Bytes - self->m_SampleBytes) * 1000.0 / (double)interval : 0;
                self->m_History[self->m_HistoryNext] = rate;
                self->m_HistoryNext = (self->m_HistoryNext + 1) % _countof(self->m_History);
                self->m_HistoryCount = min(self->m_HistoryCount + 1, _countof(self->m_History));
                self->m_SampleTime = now;
                self->m_SampleBytes = self->m_Bytes;
                UINT window = min(self->m_HistoryCount, 4u);
                double sum = 0;
                for (UINT i = 1; i <= window; ++i)
                    sum += self->m_History[(self->m_HistoryNext + _countof(self->m_History) - i) % _countof(self->m_History)];
                self->m_Speed = window ? sum / window : 0;
            }
            self->Refresh();
            if (now - self->m_Start >= 500 && !IsWindowVisible(window)) ShowWindow(window, SW_SHOWNOACTIVATE);
            state = self->m_Status == PDOPS_PAUSED ? TBPF_PAUSED : self->m_Status == PDOPS_ERRORS ? TBPF_ERROR : (self->m_Mode & (PDM_PREFLIGHT | PDM_INDETERMINATE)) ? TBPF_INDETERMINATE : TBPF_NORMAL;
            points = self->m_Points;
            total = self->m_TotalPoints;
        }
        // Never call another COM object while holding the progress state lock.
        if (self->m_Taskbar)
        {
            self->m_Taskbar->SetProgressState(window, state);
            if (total) self->m_Taskbar->SetProgressValue(window, min(points, total), total);
        }
        return TRUE;
    }
    if (message == WM_NOTIFY)
    {
        NMLINK *link = reinterpret_cast<NMLINK *>(lparam);
        if ((link->hdr.idFrom == IDC_OP_SOURCE || link->hdr.idFrom == IDC_OP_TARGET) && (link->hdr.code == NM_CLICK || link->hdr.code == NM_RETURN))
        {
            CStringW path;
            {
                ProgressLock lock(&self->m_Lock);
                path = link->hdr.idFrom == IDC_OP_SOURCE ? self->m_SourcePath : self->m_TargetPath;
            }
            if (!path.IsEmpty()) ShellExecuteW(window, L"open", path, NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
    }
    if (message == WM_COMMAND || message == WM_CLOSE)
    {
        ProgressLock lock(&self->m_Lock);
        UINT id = message == WM_CLOSE ? IDCANCEL : LOWORD(wparam);
        if (id == IDC_OP_DETAILS)
        {
            self->m_Expanded = !self->m_Expanded;
            self->Layout();
        }
        if (id == IDC_OP_PAUSE && !self->m_Cancelled && (self->m_Flags & OPPROGDLG_ENABLEPAUSE))
        {
            self->m_Status = self->m_Status == PDOPS_PAUSED ? PDOPS_RUNNING : PDOPS_PAUSED;
            self->Pause(self->m_Status == PDOPS_PAUSED);
        }
        if (id == IDCANCEL && !(self->m_Flags & PROGDLG_NOCANCEL))
        {
            self->m_Status = self->m_Cancelled ? PDOPS_STOPPED : PDOPS_CANCELLED;
            self->m_Cancelled = TRUE;
            self->Pause(FALSE);
        }
        self->Refresh();
        return TRUE;
    }
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN)
    {
        SetBkColor((HDC)wparam, self->m_Palette.Back);
        SetTextColor((HDC)wparam, self->m_Palette.Text);
        return (INT_PTR)self->m_Brush;
    }
    if (message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE ||
        (message == WM_SETTINGCHANGE && wparam == SPI_SETHIGHCONTRAST))
    {
        if (self->m_Theming) return TRUE;
        ProgressLock lock(&self->m_Lock);
        self->ApplyTheme();
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)FluentIcon(self->Glyph(IDI_OP_COPY), GetSystemMetrics(SM_CXSMICON)));
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)FluentIcon(self->Glyph(IDI_OP_COPY), GetSystemMetrics(SM_CXICON)));
        return TRUE;
    }
    if (message == WM_DRAWITEM)
    {
        ProgressLock lock(&self->m_Lock);
        DRAWITEMSTRUCT *draw = reinterpret_cast<DRAWITEMSTRUCT *>(lparam);
        if (draw->CtlID == IDC_OP_GRAPH) self->PaintGraph(draw->hDC, draw->rcItem);
        else self->PaintButton(draw);
        return TRUE;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT rect;
        GetClientRect(window, &rect);
        rect.top = rect.bottom - self->Scale(54);
        rect.bottom = rect.top + 1;
        Fill(dc, rect, self->m_Palette.Line);
        EndPaint(window, &paint);
        return TRUE;
    }
    return FALSE;
}

EXTERN_C HRESULT ProgressDialog_Constructor(IUnknown *outer, IUnknown **result)
{
    if (!result) return E_POINTER;
    *result = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    CComObject<COperationsProgressDialog> *object;
    HRESULT hr = CComObject<COperationsProgressDialog>::CreateInstance(&object);
    if (FAILED(hr)) return hr;
    return object->QueryInterface(IID_PPV_ARG(IUnknown, result));
}
