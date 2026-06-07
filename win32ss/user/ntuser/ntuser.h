#pragma once

/* UserEnterCo re-acquires the global USER lock after a usermode callout in the
 * mode it was held before (see ntuser.c); it is a real function, not an alias
 * for UserEnterExclusive. UserLeaveCo is a plain (mode-agnostic) release. */
#define UserLeaveCo UserLeave

typedef VOID (*TL_FN_FREE)(PVOID);

/* Thread Lock structure */
typedef struct _TL
{
    struct _TL* next;
    PVOID pobj;
    TL_FN_FREE pfnFree;
} TL, *PTL;

extern PSERVERINFO gpsi;
/* Per-thread (valid under the shared USER lock); was a global set on exclusive entry. */
#define gptiCurrent ((PTHREADINFO)PsGetCurrentThreadWin32Thread())
extern PPROCESSINFO gppiList;
extern PPROCESSINFO ppiScrnSaver;
extern PPROCESSINFO gppiInputProvider;
extern BOOL g_AlwaysDisplayVersion;
extern ATOM gaGuiConsoleWndClass;
extern ATOM AtomDDETrack;
extern ATOM AtomQOS;
extern ATOM AtomImeLevel;
extern ERESOURCE UserLock;

CODE_SEG("INIT") NTSTATUS NTAPI InitUserImpl(VOID);
VOID FASTCALL CleanupUserImpl(VOID);
VOID FASTCALL UserEnterShared(VOID);
VOID FASTCALL UserEnterExclusive(VOID);
VOID FASTCALL UserEnterCo(VOID);
VOID FASTCALL UserLeave(VOID);
/* Per-desktop lock (taken inside the global UserLock; global outer, desktop inner). */
VOID FASTCALL UserEnterDesktopShared(struct _DESKTOP *pdesk);
VOID FASTCALL UserEnterDesktopExclusive(struct _DESKTOP *pdesk);
VOID FASTCALL UserLeaveDesktop(struct _DESKTOP *pdesk);
BOOL FASTCALL UserIsEntered(VOID);
BOOL FASTCALL UserIsEnteredExclusive(VOID);
DWORD FASTCALL UserGetLanguageToggle(_In_ LPCWSTR pszType, _In_ DWORD dwDefaultValue);

_Success_(return != FALSE)
BOOL
NTAPI
RegReadUserSetting(
    _In_z_ PCWSTR pwszKeyName,
    _In_z_ PCWSTR pwszValueName,
    _In_ ULONG ulType,
    _Out_writes_bytes_(cjDataSize) _When_(ulType == REG_SZ, _Post_z_) PVOID pvData,
    _In_ ULONG cjDataSize);

_Success_(return != FALSE)
BOOL
NTAPI
RegWriteUserSetting(
    _In_z_ PCWSTR pwszKeyName,
    _In_z_ PCWSTR pwszValueName,
    _In_ ULONG ulType,
    _In_reads_bytes_(cjDataSize) const VOID *pvData,
    _In_ ULONG cjDataSize);

PGRAPHICS_DEVICE
NTAPI
InitDisplayDriver(
    IN PWSTR pwszDeviceName,
    IN PWSTR pwszRegKey);

/* EOF */
