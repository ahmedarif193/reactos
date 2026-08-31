#pragma once

typedef struct _SM2_FLYOUT_PALETTE
{
    COLORREF PanelBg;
    COLORREF PanelText;
    COLORREF DimText;
    COLORREF HotFill;
    COLORREF HotBorder;
    COLORREF Border;
    COLORREF AccentBg;
    COLORREF AccentText;
} SM2_FLYOUT_PALETTE;

VOID StartMenu2_GetFlyoutPalette(OUT SM2_FLYOUT_PALETTE *pPal);

HRESULT StartMenu2_Create(IN ITrayWindow *Tray, IN HWND hwndTray);
HRESULT StartMenu2_Popup(IN const RECT *prcStartBtn, IN UINT uPosition);
VOID StartMenu2_Hide(VOID);
BOOL StartMenu2_IsVisible(VOID);
VOID StartMenu2_Destroy(VOID);
