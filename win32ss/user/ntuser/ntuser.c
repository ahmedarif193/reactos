/*
 *  COPYRIGHT:        See COPYING in the top level directory
 *  PROJECT:          ReactOS kernel
 *  PURPOSE:          ntuser init. and main funcs.
 *  FILE:             win32ss/user/ntuser/ntuser.c
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserMisc);

BOOL FASTCALL RegisterControlAtoms(VOID);

/* GLOBALS ********************************************************************/

PPROCESSINFO gppiInputProvider = NULL;
BOOL g_AlwaysDisplayVersion = FALSE;
USER_CRIT gUserCrit;
USER_DOMAIN_LOCK gUserDomainLocks[DLT_MAX];
ATOM AtomMessage;       // Window Message atom.
ATOM AtomWndObj;        // Window Object atom.
ATOM AtomLayer;         // Window Layer atom.
ATOM AtomFlashWndState; // Window Flash State atom.
ATOM AtomDDETrack;      // Window DDE Tracking atom.
ATOM AtomQOS;           // Window DDE Quality of Service atom.
HINSTANCE hModClient = NULL;
BOOL ClientPfnInit = FALSE;
ATOM gaGuiConsoleWndClass;
ATOM AtomImeLevel;
ATOM AtomTouchWindow;
ATOM AtomDwmDarkMode;
ATOM AtomDwmSystemBackdropType;
ATOM AtomDwmBackdropOpacity;
ATOM AtomDwmBackdropColor;
ATOM AtomDwmBackdropColorization;
ATOM AtomDwmBackdropRegion;
ATOM AtomDwmContentBackdrop;
ATOM AtomDwmBackdropNcExtend;
ATOM AtomDwmBackdropNcExtendLeft;
ATOM AtomDwmCornerRadius;

/* PRIVATE FUNCTIONS **********************************************************/

static const PCWSTR PredefinedGlobalAtoms[] =
{
    L"StdExit",
    L"StdNewDocument",
    L"StdOpenDocument",
    L"StdEditDocument",
    L"StdNewfromTemplate",
    L"StdCloseDocument",
    L"StdShowItem",
    L"StdDoVerbItem",
    L"System",
    L"OLEsystem",
    L"StdDocumentName",
    L"Protocols",
    L"Topics",
    L"Formats",
    L"Status",
    L"EditEnvItems",
    L"True",
    L"False",
    L"Change",
    L"Save",
    L"Close",
    L"MSDraw",
    L"CC32SubclassInfo",
};

static const PCWSTR PredefinedUserAtoms[] =
{
    L"USER32",
    L"ObjectLink",
    L"OwnerLink",
    L"Native",
    L"Binary",
    L"FileName",
    L"FileNameW",
    L"NetworkName",
    L"DataObject",
    L"Embedded Object",
    L"Embed Source",
    L"Custom Link Source",
    L"Link Source",
    L"Object Descriptor",
    L"Link Source Descriptor",
    L"OleDraw",
    L"PBrush",
    L"MSDraw",
    L"Ole Private Data",
    L"Screen Picture",
    L"OleClipboardPersistOnFlush",
    L"MoreOlePrivateData",
};

static
ATOM FASTCALL
IntAddUserPropertyAtom(PCWSTR AtomName)
{
    RTL_ATOM Atom = 0;
    NTSTATUS Status;

    Status = NtAddAtom((PWSTR)AtomName,
                       (ULONG)(wcslen(AtomName) * sizeof(WCHAR)),
                       &Atom);
    if (!NT_SUCCESS(Status))
        ERR("Failed to add USER property atom %S, Status 0x%08lx\n",
            AtomName,
            Status);

    return Atom;
}

static
NTSTATUS FASTCALL
InitUserAtoms(VOID)
{
    ULONG i;

    /* Initialize the public and USER-private well-known atom namespaces. */
    for (i = 0; i < RTL_NUMBER_OF(PredefinedGlobalAtoms); i++)
        IntAddUserPropertyAtom(PredefinedGlobalAtoms[i]);

    for (i = 0; i < RTL_NUMBER_OF(PredefinedUserAtoms); i++)
        IntAddGlobalAtom((PWSTR)PredefinedUserAtoms[i], TRUE);

    RegisterControlAtoms();

    gpsi->atomSysClass[ICLS_MENU]      = 32768;
    gpsi->atomSysClass[ICLS_DESKTOP]   = 32769;
    gpsi->atomSysClass[ICLS_DIALOG]    = 32770;
    gpsi->atomSysClass[ICLS_SWITCH]    = 32771;
    gpsi->atomSysClass[ICLS_ICONTITLE] = 32772;
    gpsi->atomSysClass[ICLS_TOOLTIPS]  = 32774;

    /* System Message Atom */
    AtomMessage = IntAddGlobalAtom(L"Message", TRUE);
    gpsi->atomSysClass[ICLS_HWNDMESSAGE] = AtomMessage;

    /* System Context Help Id Atom */
    gpsi->atomContextHelpIdProp = IntAddGlobalAtom(L"SysCH", TRUE);

    gpsi->atomIconSmProp = IntAddGlobalAtom(L"SysICS", TRUE);
    gpsi->atomIconProp   = IntAddGlobalAtom(L"SysIC", TRUE);

    gpsi->atomFrostedWindowProp = IntAddGlobalAtom(L"SysFrostedWindow", TRUE);

    AtomDDETrack = IntAddGlobalAtom(L"SysDT", TRUE);
    AtomQOS      = IntAddGlobalAtom(L"SysQOS", TRUE);
    AtomImeLevel = IntAddGlobalAtom(L"SysIMEL", TRUE);
    AtomTouchWindow = IntAddGlobalAtom(L"ReactOS.TouchWindow", TRUE);
    AtomDwmDarkMode = IntAddUserPropertyAtom(L"ReactOS.Dwm.ImmersiveDarkMode");
    AtomDwmSystemBackdropType = IntAddUserPropertyAtom(DWM_PROP_SYSTEM_BACKDROP_TYPE);
    AtomDwmBackdropOpacity = IntAddUserPropertyAtom(DWM_PROP_BACKDROP_OPACITY);
    AtomDwmBackdropColor = IntAddUserPropertyAtom(DWM_PROP_BACKDROP_COLOR);
    AtomDwmBackdropColorization = IntAddUserPropertyAtom(
        DWM_PROP_BACKDROP_COLORIZATION);
    AtomDwmBackdropRegion = IntAddUserPropertyAtom(DWM_PROP_BACKDROP_REGION);
    AtomDwmContentBackdrop = IntAddUserPropertyAtom(DWM_PROP_CONTENT_BACKDROP);
    AtomDwmBackdropNcExtend = IntAddUserPropertyAtom(
        DWM_PROP_BACKDROP_NC_EXTEND);
    AtomDwmBackdropNcExtendLeft = IntAddUserPropertyAtom(
        DWM_PROP_BACKDROP_NC_EXTEND_LEFT);
    AtomDwmCornerRadius = IntAddUserPropertyAtom(DWM_PROP_CORNER_RADIUS);

    /*
     * FIXME: AddPropW uses the global kernel atom table, thus leading to conflicts if we use
     * the win32k atom table for this one. What is the right thing to do ?
     */
    // AtomWndObj = IntAddGlobalAtom(L"SysWNDO", TRUE);
    NtAddAtom(L"SysWNDO", 14, &AtomWndObj);

    AtomLayer = IntAddGlobalAtom(L"SysLayer", TRUE);
    AtomFlashWndState = IntAddGlobalAtom(L"FlashWState", TRUE);

    return STATUS_SUCCESS;
}

/* FUNCTIONS ******************************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
InitUserImpl(VOID)
{
    NTSTATUS Status;
    HKEY hKey;

    if (!UserCreateHandleTable())
    {
        ERR("Failed creating handle table\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = InitSessionImpl();
    if (!NT_SUCCESS(Status))
    {
        ERR("Error init session impl.\n");
        return Status;
    }

    InitUserAtoms();

    Status = RegOpenKey(L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                        &hKey);
    if (NT_SUCCESS(Status))
    {
        DWORD dwValue = 0;
        RegReadDWORD(hKey, L"DisplayVersion", &dwValue);
        g_AlwaysDisplayVersion = !!dwValue;
        ZwClose(hKey);
    }

    InitSysParams();

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
UserInitialize(VOID)
{
    static const DWORD wPattern55AA[] = /* 32 bit aligned */
    { 0x55555555, 0xaaaaaaaa, 0x55555555, 0xaaaaaaaa,
      0x55555555, 0xaaaaaaaa, 0x55555555, 0xaaaaaaaa };
    HBITMAP hPattern55AABitmap = NULL;
    NTSTATUS Status;

    NT_ASSERT(PsGetCurrentThreadWin32Thread() != NULL);

// Create Event for Disconnect Desktop.

    Status = UserCreateWinstaDirectory();
    if (!NT_SUCCESS(Status)) return Status;

    /* Initialize the Video */
    Status = InitVideo();
    if (!NT_SUCCESS(Status))
    {
        /* We failed, bugcheck */
        KeBugCheckEx(VIDEO_DRIVER_INIT_FAILURE, Status, 0, 0, USER_VERSION);
    }

// {
//     DrvInitConsole.
//     DrvChangeDisplaySettings.
//     Update Shared Device Caps.
//     Initialize User Screen.
// }

// Set Global SERVERINFO Error flags.
// Load Resources.

    NtUserUpdatePerUserSystemParameters(0, TRUE);

    if (gpsi->hbrGray == NULL)
    {
        hPattern55AABitmap = GreCreateBitmap(8, 8, 1, 1, (LPBYTE)wPattern55AA);
        if (hPattern55AABitmap == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        gpsi->hbrGray = IntGdiCreatePatternBrush(hPattern55AABitmap);

        if (gpsi->hbrGray == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

/*
 * Called from usersrv.
 */
NTSTATUS
APIENTRY
NtUserInitialize(
    DWORD  dwWinVersion,
    HANDLE hPowerRequestEvent,
    HANDLE hMediaRequestEvent)
{
    NTSTATUS Status;

    TRACE("Enter NtUserInitialize(%lx, %p, %p)\n",
          dwWinVersion, hPowerRequestEvent, hMediaRequestEvent);

    /* Check if we are already initialized */
    if (gpepCSRSS)
        return STATUS_UNSUCCESSFUL;

    /* Check Windows USER subsystem version */
    if (dwWinVersion != USER_VERSION)
    {
        /* No match, bugcheck */
        KeBugCheckEx(WIN32K_INIT_OR_RIT_FAILURE, 0, 0, dwWinVersion, USER_VERSION);
    }

    /* Acquire exclusive lock */
    UserEnterExclusive();

    /* Save the EPROCESS of CSRSS */
    InitCsrProcess(/*PsGetCurrentProcess()*/);

    /* Initialize Power Request List */
    Status = IntInitWin32PowerManagement(hPowerRequestEvent);
    if (!NT_SUCCESS(Status))
    {
        UserLeave();
        return Status;
    }

// Initialize Media Change (use hMediaRequestEvent).

    /* Initialize various GDI stuff (DirectX, fonts, language ID etc.) */
    if (!InitializeGreCSRSS())
        return STATUS_UNSUCCESSFUL;

    /* Initialize USER */
    Status = UserInitialize();

    /* Return */
    UserLeave();
    return Status;
}


/*
RETURN
   True if current thread owns the lock (possibly shared)
*/
VOID FASTCALL UserInitCrit(VOID)
{
    ExInitializePushLock(&gUserCrit.Lock);
}

BOOL FASTCALL UserIsEnteredExclusive(VOID)
{
    return gUserCrit.OwnerExclusive == KeGetCurrentThread();
}

BOOL FASTCALL UserIsEnteredShared(VOID)
{
    PTHREADINFO pti = PsGetCurrentThreadWin32Thread();

    return !UserIsEnteredExclusive() && pti && pti->cSharedCrit != 0;
}

BOOL FASTCALL UserIsEntered(VOID)
{
    return UserIsEnteredExclusive() || UserIsEnteredShared();
}

VOID FASTCALL CleanupUserImpl(VOID)
{
}

VOID FASTCALL UserEnterShared(VOID)
{
    PTHREADINFO pti;

    if (gUserCrit.OwnerExclusive == KeGetCurrentThread())
    {
        gUserCrit.RecursionExclusive++;
        return;
    }
    pti = PsGetCurrentThreadWin32Thread();
    if (!pti)
    {
        UserEnterExclusive();
        return;
    }
    if (pti->cSharedCrit)
    {
        pti->cSharedCrit++;
        return;
    }
    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&gUserCrit.Lock);
    gUserCrit.HandoffPending = 0;
    pti->cSharedCrit = 1;
}

static VOID FASTCALL UserCritWaitHandoff(PKTHREAD Thread)
{
    LARGE_INTEGER Freq, Now;
    LONGLONG Deadline;

    Now = KeQueryPerformanceCounter(&Freq);
    Deadline = Now.QuadPart + Freq.QuadPart / 20000;
    while (gUserCrit.HandoffPending && gUserCrit.LastOwner == Thread)
    {
        YieldProcessor();
        Now = KeQueryPerformanceCounter(NULL);
        if (Now.QuadPart >= Deadline)
            break;
    }
}

VOID FASTCALL UserEnterExclusive(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT_NOGDILOCKS();
    if (gUserCrit.OwnerExclusive == Thread)
    {
        gUserCrit.RecursionExclusive++;
        return;
    }
    ASSERT(!UserIsEnteredShared());
    if (gUserCrit.HandoffPending && gUserCrit.LastOwner == Thread)
        UserCritWaitHandoff(Thread);
    KeEnterCriticalRegion();
    ExfAcquirePushLockExclusive(&gUserCrit.Lock);
    gUserCrit.HandoffPending = 0;
    gUserCrit.OwnerExclusive = Thread;
    gUserCrit.RecursionExclusive = 1;
}

VOID FASTCALL UserLeave(VOID)
{
    PTHREADINFO pti = PsGetCurrentThreadWin32Thread();

    ASSERT_NOGDILOCKS();
    ASSERT(UserIsEntered());
    if (gUserCrit.OwnerExclusive == KeGetCurrentThread())
    {
        if (--gUserCrit.RecursionExclusive)
            return;
        gUserCrit.OwnerExclusive = NULL;
        gUserCrit.LastOwner = KeGetCurrentThread();
        if (gUserCrit.Lock.Waiting)
            InterlockedExchange(&gUserCrit.HandoffPending, 1);
        ExfReleasePushLockExclusive(&gUserCrit.Lock);
        KeLeaveCriticalRegion();
    }
    else
    {
        ASSERT(pti && pti->cSharedCrit);
        if (--pti->cSharedCrit)
            return;
        ExfReleasePushLockShared(&gUserCrit.Lock);
        KeLeaveCriticalRegion();
    }

    if (pti && pti->DeferredFreeList.Next && !UserIsEntered())
        UserProcessDeferredFrees(pti);
}

VOID FASTCALL UserLeaveCo(VOID)
{
    PTHREADINFO pti = PsGetCurrentThreadWin32Thread();

    if (pti)
    {
        ULONG Depth = pti->cCritDispositionDepth++;

        if (Depth < sizeof(ULONG_PTR) * 8)
        {
            if (UserIsEnteredExclusive())
                pti->ulCritDisposition |= ((ULONG_PTR)1 << Depth);
            else
                pti->ulCritDisposition &= ~((ULONG_PTR)1 << Depth);
        }
    }
    UserLeave();
}

VOID FASTCALL UserEnterCo(VOID)
{
    PTHREADINFO pti = PsGetCurrentThreadWin32Thread();
    BOOL Exclusive = TRUE;

    if (pti && pti->cCritDispositionDepth)
    {
        ULONG Depth = --pti->cCritDispositionDepth;

        if (Depth < sizeof(ULONG_PTR) * 8)
            Exclusive = (pti->ulCritDisposition >> Depth) & 1;
    }
    if (Exclusive)
        UserEnterExclusive();
    else
        UserEnterShared();
}

VOID FASTCALL UserInitDomainLocks(VOID)
{
    ULONG i;

    for (i = 0; i < DLT_MAX; i++)
    {
        ExInitializePushLock(&gUserDomainLocks[i].Lock);
        gUserDomainLocks[i].OwnerExclusive = NULL;
        gUserDomainLocks[i].RecursionExclusive = 0;
    }
}

VOID FASTCALL UserDomainLockExclusive(USER_DOMAIN_LOCK_TYPE Type)
{
    PUSER_DOMAIN_LOCK pLock = &gUserDomainLocks[Type];
    PKTHREAD Thread = KeGetCurrentThread();

    if (UserIsEnteredExclusive())
        return;
    if (pLock->OwnerExclusive == Thread)
    {
        pLock->RecursionExclusive++;
        return;
    }
    ExfAcquirePushLockExclusive(&pLock->Lock);
    pLock->OwnerExclusive = Thread;
    pLock->RecursionExclusive = 1;
}

VOID FASTCALL UserDomainUnlockExclusive(USER_DOMAIN_LOCK_TYPE Type)
{
    PUSER_DOMAIN_LOCK pLock = &gUserDomainLocks[Type];

    if (UserIsEnteredExclusive())
        return;
    ASSERT(pLock->OwnerExclusive == KeGetCurrentThread());
    if (--pLock->RecursionExclusive)
        return;
    pLock->OwnerExclusive = NULL;
    ExfReleasePushLockExclusive(&pLock->Lock);
}

VOID FASTCALL UserDomainLockShared(USER_DOMAIN_LOCK_TYPE Type)
{
    PUSER_DOMAIN_LOCK pLock = &gUserDomainLocks[Type];

    if (UserIsEnteredExclusive())
        return;
    if (pLock->OwnerExclusive == KeGetCurrentThread())
        return;
    ExfAcquirePushLockShared(&pLock->Lock);
}

VOID FASTCALL UserDomainUnlockShared(USER_DOMAIN_LOCK_TYPE Type)
{
    PUSER_DOMAIN_LOCK pLock = &gUserDomainLocks[Type];

    if (UserIsEnteredExclusive())
        return;
    if (pLock->OwnerExclusive == KeGetCurrentThread())
        return;
    ExfReleasePushLockShared(&pLock->Lock);
}

/* EOF */
