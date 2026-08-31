#pragma once

HRESULT StartMenu2_Create(IN ITrayWindow *Tray, IN HWND hwndTray);
HRESULT StartMenu2_Popup(IN const RECT *prcStartBtn, IN UINT uPosition);
VOID StartMenu2_Hide(VOID);
BOOL StartMenu2_IsVisible(VOID);
VOID StartMenu2_Destroy(VOID);
