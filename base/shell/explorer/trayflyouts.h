#pragma once

VOID TaskPreview_Show(IN HWND hwndOwner, IN const RECT *prcAnchor,
                      IN const HWND *pahWnd, IN UINT cWindows, IN INT_PTR nGroupId);
VOID TaskPreview_ShowHover(IN HWND hwndOwner, IN const RECT *prcAnchor,
                           IN const HWND *pahWnd, IN UINT cWindows, IN INT_PTR nGroupId);
VOID TaskPreview_Hide(VOID);
BOOL TaskPreview_IsVisibleFor(IN INT_PTR nGroupId);
BOOL TaskPreview_IsHover(VOID);

VOID TrayCalendar_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
BOOL TrayCalendar_IsOpen(VOID);
VOID TrayVolume_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayVolume_SetCachedState(IN int nPercent, IN BOOL bMute);
VOID TrayNetwork_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayMixer_Open(IN const RECT *prcAnchor);
VOID TrayPower_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);

VOID TrayNotifications_Add(IN HWND hWndOwner, IN UINT uID, IN LPCWSTR pszApp,
                           IN LPCWSTR pszTitle, IN LPCWSTR pszText, IN HICON hIcon);
UINT TrayNotifications_GetUnread(VOID);
VOID TrayNotifications_SetSink(IN HWND hwndSink);

VOID TrayOverflow_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor, IN HWND hwndPager);
BOOL TrayOverflow_IsOpen(VOID);
VOID TrayOverflow_Refresh(VOID);

VOID TrayQuickSettings_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
BOOL TrayQuickSettings_IsOpen(VOID);

VOID TrayFlyouts_Destroy(VOID);
VOID TrayFlyoutsAux_Destroy(VOID);
