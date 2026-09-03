#pragma once

VOID TaskPreview_Show(IN HWND hwndOwner, IN const RECT *prcAnchor,
                      IN const HWND *pahWnd, IN UINT cWindows, IN INT_PTR nGroupId);
VOID TaskPreview_Hide(VOID);
BOOL TaskPreview_IsVisibleFor(IN INT_PTR nGroupId);

VOID TrayCalendar_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayVolume_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayNetwork_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayMixer_Open(IN const RECT *prcAnchor);
VOID TrayPower_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);

VOID TrayFlyouts_Destroy(VOID);
VOID TrayFlyoutsAux_Destroy(VOID);
