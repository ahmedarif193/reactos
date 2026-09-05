#pragma once

typedef enum _USER_DOMAIN_LOCK_TYPE
{
    DLT_HANDLEMANAGER,
    DLT_ASYNCKEYSTATE,
    DLT_QUEUE,
    DLT_POST,
    DLT_HOOK,
    DLT_WINEVENT,
    DLT_DESKTOP,
    DLT_THREADINFO,
    DLT_FOREGROUND,
    DLT_MAX
} USER_DOMAIN_LOCK_TYPE;

typedef struct _USER_DOMAIN_LOCK
{
    EX_PUSH_LOCK Lock;
    PKTHREAD OwnerExclusive;
    ULONG RecursionExclusive;
} USER_DOMAIN_LOCK, *PUSER_DOMAIN_LOCK;

typedef VOID (*TL_FN_FREE)(PVOID);

/* Thread Lock structure */
typedef struct _TL
{
    struct _TL* next;
    PVOID pobj;
    TL_FN_FREE pfnFree;
} TL, *PTL;

extern PSERVERINFO gpsi;
#define gptiCurrent ((PTHREADINFO)PsGetCurrentThreadWin32Thread())
extern PPROCESSINFO gppiList;
extern PPROCESSINFO ppiScrnSaver;
extern PPROCESSINFO gppiInputProvider;
extern BOOL g_AlwaysDisplayVersion;
extern ATOM gaGuiConsoleWndClass;
extern ATOM AtomDDETrack;
extern ATOM AtomQOS;
extern ATOM AtomImeLevel;
extern ATOM AtomDwmDarkMode;
extern ATOM AtomDwmSystemBackdropType;
extern ATOM AtomDwmBackdropOpacity;
extern ATOM AtomDwmBackdropColor;
extern ATOM AtomDwmBackdropColorization;
extern ATOM AtomDwmBackdropRegion;
extern ATOM AtomDwmBackdropNcExtend;
extern ATOM AtomDwmCornerRadius;
typedef struct _USER_CRIT
{
    EX_PUSH_LOCK Lock;
    PKTHREAD OwnerExclusive;
    ULONG RecursionExclusive;
    PKTHREAD LastOwner;
    volatile LONG HandoffPending;
} USER_CRIT, *PUSER_CRIT;

extern USER_CRIT gUserCrit;
VOID FASTCALL UserInitCrit(VOID);

CODE_SEG("INIT") NTSTATUS NTAPI InitUserImpl(VOID);
VOID FASTCALL CleanupUserImpl(VOID);
VOID FASTCALL UserEnterShared(VOID);
VOID FASTCALL UserEnterExclusive(VOID);
VOID FASTCALL UserLeave(VOID);
BOOL FASTCALL UserIsEntered(VOID);
BOOL FASTCALL UserIsEnteredExclusive(VOID);
BOOL FASTCALL UserIsEnteredShared(VOID);
VOID FASTCALL UserEnterCo(VOID);
VOID FASTCALL UserLeaveCo(VOID);
VOID FASTCALL UserInitDomainLocks(VOID);
VOID FASTCALL UserDomainLockExclusive(USER_DOMAIN_LOCK_TYPE Type);
VOID FASTCALL UserDomainUnlockExclusive(USER_DOMAIN_LOCK_TYPE Type);
VOID FASTCALL UserDomainLockShared(USER_DOMAIN_LOCK_TYPE Type);
VOID FASTCALL UserDomainUnlockShared(USER_DOMAIN_LOCK_TYPE Type);
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
