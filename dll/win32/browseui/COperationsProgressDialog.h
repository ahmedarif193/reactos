/*
 * PROJECT:     ReactOS browseui
 * LICENSE:     GPL-3.0-or-later
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Shell operation progress UI and public COM interfaces
 */
#pragma once

#include <uxtheme.h>

typedef struct _OP_PALETTE
{
    BOOL Dark;
    BOOL Material;
    COLORREF Back;
    COLORREF Text;
    COLORREF Dim;
    COLORREF Line;
    COLORREF Hot;
    COLORREF Press;
    COLORREF Track;
    COLORREF Border;
} OP_PALETTE;

class COperationsProgressDialog :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IOperationsProgressDialog,
    public IProgressDialog,
    public IOleWindow
{
    CRITICAL_SECTION m_Lock, m_Lifecycle;
    HWND m_Window, m_Owner, m_DisabledOwner;
    HANDLE m_Thread, m_Ready;
    HRESULT m_CreateResult;
    DWORD m_Flags;
    BOOL m_Modern, m_ModernStarted, m_Expanded, m_TimerPaused, m_Cancelled, m_Theming;
    PDMODE m_Mode;
    PDOPSTATUS m_Status;
    SPACTION m_Action;
    ULONGLONG m_Points, m_TotalPoints, m_Bytes, m_TotalBytes, m_Items, m_TotalItems;
    ULONGLONG m_Start, m_PauseStart, m_PausedTime, m_SampleTime, m_SampleBytes;
    double m_Speed, m_History[120];
    UINT m_HistoryCount, m_HistoryNext, m_Dpi;
    CStringW m_Title, m_Lines[3], m_CancelText, m_Source, m_Target, m_Item, m_SourcePath, m_TargetPath;
    HFONT m_Font, m_LargeFont;
    HBRUSH m_Brush;
    HTHEME m_Theme;
    OP_PALETTE m_Palette;
    CComPtr<ITaskbarList3> m_Taskbar;

    static DWORD WINAPI ThreadProc(void *parameter);
    static INT_PTR CALLBACK DialogProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    HRESULT Start(HWND owner, DWORD flags, BOOL modern);
    void Notify();
    void Layout();
    void LayoutLocations(const CStringW &prefix, BOOL source, BOOL target);
    void Refresh();
    void ApplyTheme();
    UINT Glyph(UINT id) const;
    void PaintGraphFrame(HDC dc, RECT rect, HRGN clip, int radius);
    void PaintGraph(HDC dc, RECT rect);
    void PaintButton(DRAWITEMSTRUCT *draw);
    void Pause(BOOL paused);
    ULONGLONG Elapsed() const;
    ULONGLONG Remaining() const;
    int Scale(int value) const { return MulDiv(value, m_Dpi, 96); }
    CStringW Text(UINT id) const;

public:
    COperationsProgressDialog();
    ~COperationsProgressDialog();

    STDMETHOD(StartProgressDialog)(HWND owner, OPPROGDLGF flags) override;
    STDMETHOD(StopProgressDialog)() override;
    STDMETHOD(SetOperation)(SPACTION action) override;
    STDMETHOD(SetMode)(PDMODE mode) override;
    STDMETHOD(UpdateProgress)(ULONGLONG points, ULONGLONG totalPoints, ULONGLONG bytes, ULONGLONG totalBytes, ULONGLONG items, ULONGLONG totalItems) override;
    STDMETHOD(UpdateLocations)(IShellItem *source, IShellItem *target, IShellItem *item) override;
    STDMETHOD(ResetTimer)() override;
    STDMETHOD(PauseTimer)() override;
    STDMETHOD(ResumeTimer)() override;
    STDMETHOD(GetMilliseconds)(ULONGLONG *elapsed, ULONGLONG *remaining) override;
    STDMETHOD(GetOperationStatus)(PDOPSTATUS *status) override;

    STDMETHOD(StartProgressDialog)(HWND owner, IUnknown *modeless, DWORD flags, LPCVOID reserved) override;
    STDMETHOD(SetTitle)(LPCWSTR title) override;
    STDMETHOD(SetAnimation)(HINSTANCE instance, UINT resource) override;
    STDMETHOD_(BOOL, HasUserCancelled)() override;
    STDMETHOD(SetProgress)(DWORD completed, DWORD total) override;
    STDMETHOD(SetProgress64)(ULONGLONG completed, ULONGLONG total) override;
    STDMETHOD(SetLine)(DWORD line, LPCWSTR text, BOOL path, LPCVOID reserved) override;
    STDMETHOD(SetCancelMsg)(LPCWSTR text, LPCVOID reserved) override;
    STDMETHOD(Timer)(DWORD action, LPCVOID reserved) override;
    STDMETHOD(GetWindow)(HWND *window) override;
    STDMETHOD(ContextSensitiveHelp)(BOOL enter) override;

    BEGIN_COM_MAP(COperationsProgressDialog)
        COM_INTERFACE_ENTRY_IID(IID_IOperationsProgressDialog, IOperationsProgressDialog)
        COM_INTERFACE_ENTRY_IID(IID_IProgressDialog, IProgressDialog)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    END_COM_MAP()
};
