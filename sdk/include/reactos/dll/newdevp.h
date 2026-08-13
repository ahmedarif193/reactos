/*
 * newdevp.h
 *
 * Private header for newdev.dll
 *
 */

#ifndef __NEWDEVP__H
#define __NEWDEVP__H

/* Private ClientSideInstallW named-pipe protocol extension. */
#define NEWDEV_INSTALL_BATCH_MARKER 0x48435442UL
#define NEWDEV_INSTALL_BATCH_MAX_DEVICES 65536

#ifdef __cplusplus
extern "C" {
#endif

BOOL
WINAPI
DevInstallW(
    IN HWND hWndParent,
    IN HINSTANCE hInstance,
    IN LPCWSTR InstanceId,
    IN INT Show);

BOOL
WINAPI
InstallDevInst(
    IN HWND hWndParent,
    IN LPCWSTR InstanceId,
    IN BOOL bUpdate,
    OUT LPDWORD lpReboot);

BOOL
WINAPI
InstallDevInstEx(
    IN HWND hWndParent,
    IN LPCWSTR InstanceId,
    IN BOOL bUpdate,
    OUT LPDWORD lpReboot,
    IN DWORD Unknown);

#ifdef __cplusplus
}
#endif

#endif /* __NEWDEVP__H */
