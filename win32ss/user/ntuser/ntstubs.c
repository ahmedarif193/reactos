/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          Native User stubs
 * FILE:             win32ss/user/ntuser/ntstubs.c
 * PROGRAMER:        Casper S. Hornstrup (chorns@users.sourceforge.net)
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserMisc);

//
// Works like BitBlt, https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt
//
BOOL
APIENTRY
NtUserBitBltSysBmp(
   HDC hdc,
   INT nXDest,
   INT nYDest,
   INT nWidth,
   INT nHeight,
   INT nXSrc,
   INT nYSrc,
   DWORD dwRop )
{
   BOOL Ret = FALSE;
   UserEnterExclusive();

   Ret = NtGdiBitBlt( hdc,
                   nXDest,
                   nYDest,
                   nWidth,
                  nHeight,
                hSystemBM,
                    nXSrc,
                    nYSrc,
                    dwRop,
              CLR_INVALID,
                        0);

   UserLeave();
   return Ret;
}

DWORD
APIENTRY
NtUserDragObject(
   HWND    hwnd1,
   HWND    hwnd2,
   UINT    u1,
   DWORD   dw1,
   HCURSOR hc1
)
{
   STUB

   return 0;
}

BOOL
APIENTRY
NtUserDrawAnimatedRects(
   HWND hwnd,
   INT idAni,
   RECT *lprcFrom,
   RECT *lprcTo)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserEvent(
   DWORD Unknown0)
{
   STUB

   return 0;
}

BOOL
APIENTRY
NtUserGetAltTabInfo(
   HWND hwnd,
   INT  iItem,
   PALTTABINFO pati,
   LPWSTR pszItemText,
   UINT   cchItemText,
   BOOL   Ansi)
{
   STUB

   return 0;
}

NTSTATUS
APIENTRY
NtUserInitializeClientPfnArrays(
  PPFNCLIENT pfnClientA,
  PPFNCLIENT pfnClientW,
  PPFNCLIENTWORKER pfnClientWorker,
  HINSTANCE hmodUser)
{
   NTSTATUS Status = STATUS_SUCCESS;
   TRACE("Enter NtUserInitializeClientPfnArrays User32 0x%p\n", hmodUser);

   if (ClientPfnInit) return Status;

   UserEnterExclusive();

   _SEH2_TRY
   {
      ProbeForRead( pfnClientA, sizeof(PFNCLIENT), 1);
      ProbeForRead( pfnClientW, sizeof(PFNCLIENT), 1);
      ProbeForRead( pfnClientWorker, sizeof(PFNCLIENTWORKER), 1);
      RtlCopyMemory(&gpsi->apfnClientA, pfnClientA, sizeof(PFNCLIENT));
      RtlCopyMemory(&gpsi->apfnClientW, pfnClientW, sizeof(PFNCLIENT));
      RtlCopyMemory(&gpsi->apfnClientWorker, pfnClientWorker, sizeof(PFNCLIENTWORKER));

      //// FIXME: HAX! Temporary until server side is finished.
      //// Copy the client side procs for now.
      RtlCopyMemory(&gpsi->aStoCidPfn, pfnClientW, sizeof(gpsi->aStoCidPfn));

      hModClient = hmodUser;
      ClientPfnInit = TRUE;
   }
   _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
   {
      Status =_SEH2_GetExceptionCode();
   }
   _SEH2_END

   if (!NT_SUCCESS(Status))
   {
      ERR("Failed reading Client Pfns from user space.\n");
      SetLastNtError(Status);
   }

   UserLeave();
   return Status;
}

DWORD
APIENTRY
NtUserInitTask(
   DWORD Unknown0,
   DWORD Unknown1,
   DWORD Unknown2,
   DWORD Unknown3,
   DWORD Unknown4,
   DWORD Unknown5,
   DWORD Unknown6,
   DWORD Unknown7,
   DWORD Unknown8,
   DWORD Unknown9,
   DWORD Unknown10,
   DWORD Unknown11)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserMNDragLeave(VOID)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserMNDragOver(
   DWORD Unknown0,
   DWORD Unknown1)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserModifyUserStartupInfoFlags(
   DWORD Unknown0,
   DWORD Unknown1)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserQueryUserCounters(
   DWORD Unknown0,
   DWORD Unknown1,
   DWORD Unknown2,
   DWORD Unknown3,
   DWORD Unknown4)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserRegisterTasklist(
   DWORD Unknown0)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserSetConsoleReserveKeys(
   DWORD Unknown0,
   DWORD Unknown1)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserSetDbgTag(
   DWORD Unknown0,
   DWORD Unknown1)
{
   STUB;

   return 0;
}

DWORD
APIENTRY
NtUserSetDbgTagCount(
    DWORD Unknown0)
{
    STUB;

    return 0;
}

DWORD
APIENTRY
NtUserSetRipFlags(
   DWORD Unknown0)
{
   STUB;

   return 0;
}

DWORD
APIENTRY
NtUserDbgWin32HeapFail(
    DWORD Unknown0,
    DWORD Unknown1)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserDbgWin32HeapStat(
    DWORD Unknown0,
    DWORD Unknown1)
{
   STUB

   return 0;
}

BOOL
APIENTRY
NtUserSetSysColors(
   int cElements,
   IN CONST INT *lpaElements,
   IN CONST COLORREF *lpaRgbValues,
   FLONG Flags)
{
   DWORD Ret = TRUE;

   if (cElements == 0)
      return TRUE;

   /* We need this check to prevent overflow later */
   if ((ULONG)cElements >= 0x40000000)
   {
      EngSetLastError(ERROR_NOACCESS);
      return FALSE;
   }

   UserEnterExclusive();

   _SEH2_TRY
   {
      ProbeForRead(lpaElements, cElements * sizeof(INT), 1);
      ProbeForRead(lpaRgbValues, cElements * sizeof(COLORREF), 1);

      IntSetSysColors(cElements, lpaElements, lpaRgbValues);
   }
   _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
   {
      SetLastNtError(_SEH2_GetExceptionCode());
      Ret = FALSE;
   }
   _SEH2_END;

   if (Ret)
   {
      UserSendNotifyMessage(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);

      UserRedrawDesktop();
   }

   UserLeave();
   return Ret;
}

DWORD
APIENTRY
NtUserUpdateInstance(
   DWORD Unknown0,
   DWORD Unknown1,
   DWORD Unknown2)
{
   STUB

   return 0;
}

BOOL
APIENTRY
NtUserUserHandleGrantAccess(
   IN HANDLE hUserHandle,
   IN HANDLE hJob,
   IN BOOL bGrant)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserWaitForMsgAndEvent(
   DWORD Unknown0)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserWin32PoolAllocationStats(
   DWORD Unknown0,
   DWORD Unknown1,
   DWORD Unknown2,
   DWORD Unknown3,
   DWORD Unknown4,
   DWORD Unknown5)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserYieldTask(VOID)
{
   STUB

   return 0;
}

DWORD
APIENTRY
NtUserGetRawInputBuffer(
    PRAWINPUT pData,
    PUINT pcbSize,
    UINT cbSizeHeader)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserGetRawInputData(
    HRAWINPUT hRawInput,
    UINT uiCommand,
    LPVOID pData,
    PUINT pcbSize,
    UINT cbSizeHeader)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserGetRawInputDeviceInfo(
    HANDLE hDevice,
    UINT uiCommand,
    LPVOID pData,
    PUINT pcbSize
)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserGetRawInputDeviceList(
    PRAWINPUTDEVICELIST pRawInputDeviceList,
    PUINT puiNumDevices,
    UINT cbSize)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserGetRegisteredRawInputDevices(
    PRAWINPUTDEVICE pRawInputDevices,
    PUINT puiNumDevices,
    UINT cbSize)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserHardErrorControl(
    DWORD dwUnknown1,
    DWORD dwUnknown2,
    DWORD dwUnknown3)
{
    STUB;
    return 0;
}

BOOL
NTAPI
NtUserNotifyProcessCreate(
    HANDLE NewProcessId,
    HANDLE ParentThreadId,
    ULONG  dwUnknown,
    ULONG  CreateFlags)
{
    // STUB;
    TRACE("NtUserNotifyProcessCreate is UNIMPLEMENTED\n");
    return FALSE;
}

NTSTATUS
APIENTRY
NtUserProcessConnect(
    IN  HANDLE ProcessHandle,
    OUT PUSERCONNECT pUserConnect,
    IN  ULONG Size)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEPROCESS Process = NULL;
    PPROCESSINFO W32Process;

    TRACE("NtUserProcessConnect\n");

    if (pUserConnect == NULL ||
        Size != sizeof(*pUserConnect))
    {
        return STATUS_UNSUCCESSFUL;
    }

    /* Get the process object the user handle was referencing */
    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_VM_OPERATION,
                                       *PsProcessType,
                                       UserMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    UserEnterShared();

    /* Get Win32 process information */
    W32Process = PsGetProcessWin32Process(Process);

    _SEH2_TRY
    {
        UINT i;

        // FIXME: Check that pUserConnect->ulVersion == USER_VERSION;
        // FIXME: Check the value of pUserConnect->dwDispatchCount.

        ProbeForWrite(pUserConnect, sizeof(*pUserConnect), sizeof(PVOID));

        // FIXME: Instead of assuming that the mapping of the heap desktop
        // also holds there, we **MUST** create and map instead the shared
        // section! Its client base must be stored in W32Process->pClientBase.
        // What is currently done (ReactOS-specific only), is that within the
        // IntUserHeapCommitRoutine()/MapGlobalUserHeap() routines we assume
        // it's going to be also called early, so that we manually add a very
        // first memory mapping that corresponds to the "global user heap",
        // and that we use instead of a actual win32 "shared USER section"
        // (see slide 29 of https://paper.bobylive.com/Meeting_Papers/BlackHat/USA-2011/BH_US_11_Mandt_win32k_Slides.pdf )

        pUserConnect->siClient.ulSharedDelta =
            (ULONG_PTR)W32Process->HeapMappings.KernelMapping -
            (ULONG_PTR)W32Process->HeapMappings.UserMapping;

#define SERVER_TO_CLIENT(ptr) \
    ((PVOID)((ULONG_PTR)ptr - pUserConnect->siClient.ulSharedDelta))

        ASSERT(gpsi);
        ASSERT(gHandleTable);

        pUserConnect->siClient.psi       = SERVER_TO_CLIENT(gpsi);
        pUserConnect->siClient.aheList   = SERVER_TO_CLIENT(gHandleTable);
        pUserConnect->siClient.pDispInfo = NULL;

        // NOTE: kernel server should also have a SHAREDINFO gSharedInfo;
        // FIXME: These USER window-proc data should be used somehow!

        pUserConnect->siClient.DefWindowMsgs.maxMsgs     = 0;
        pUserConnect->siClient.DefWindowMsgs.abMsgs      = NULL;
        pUserConnect->siClient.DefWindowSpecMsgs.maxMsgs = 0;
        pUserConnect->siClient.DefWindowSpecMsgs.abMsgs  = NULL;

        for (i = 0; i < ARRAYSIZE(pUserConnect->siClient.awmControl); ++i)
        {
            pUserConnect->siClient.awmControl[i].maxMsgs = 0;
            pUserConnect->siClient.awmControl[i].abMsgs  = NULL;
        }
#undef SERVER_TO_CLIENT
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        SetLastNtError(Status);

    UserLeave();

    /* Dereference the process object */
    ObDereferenceObject(Process);

    return Status;
}

NTSTATUS
APIENTRY
NtUserQueryInformationThread(IN HANDLE ThreadHandle,
                             IN USERTHREADINFOCLASS ThreadInformationClass,
                             OUT PVOID ThreadInformation,
                             IN ULONG ThreadInformationLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PETHREAD Thread;

    /* Allow only CSRSS to perform this operation */
    if (PsGetCurrentProcess() != gpepCSRSS)
        return STATUS_ACCESS_DENIED;

    UserEnterExclusive();

    /* Get the Thread */
    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_QUERY_INFORMATION,
                                       *PsThreadType,
                                       UserMode,
                                       (PVOID)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) goto Quit;

    switch (ThreadInformationClass)
    {
        default:
        {
            STUB;
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }
    }

    ObDereferenceObject(Thread);

Quit:
    UserLeave();
    return Status;
}

BOOL
APIENTRY
NtUserRealInternalGetMessage(
    LPMSG lpMsg,
    HWND hWnd,
    UINT wMsgFilterMin,
    UINT wMsgFilterMax,
    UINT wRemoveMsg,
    BOOL bGMSG)
{
    STUB;
    return 0;
}

BOOL
APIENTRY
NtUserRealWaitMessageEx(
    DWORD dwWakeMask,
    UINT uTimeout)
{
    STUB;
    return 0;
}

BOOL
APIENTRY
NtUserRegisterRawInputDevices(
    IN PCRAWINPUTDEVICE pRawInputDevices,
    IN UINT uiNumDevices,
    IN UINT cbSize)
{
    STUB;
    return 0;
}

DWORD APIENTRY
NtUserResolveDesktopForWOW(DWORD Unknown0)
{
    STUB
    return 0;
}

DWORD
APIENTRY
NtUserSetInformationProcess(
    DWORD dwUnknown1,
    DWORD dwUnknown2,
    DWORD dwUnknown3,
    DWORD dwUnknown4)
{
    STUB;
    return 0;
}

HDESK FASTCALL
IntGetDesktopObjectHandle(PDESKTOP DesktopObject);

NTSTATUS
APIENTRY
NtUserSetInformationThread(IN HANDLE ThreadHandle,
                           IN USERTHREADINFOCLASS ThreadInformationClass,
                           IN PVOID ThreadInformation,
                           IN ULONG ThreadInformationLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PETHREAD Thread;

    /* Allow only CSRSS to perform this operation */
    if (PsGetCurrentProcess() != gpepCSRSS)
        return STATUS_ACCESS_DENIED;

    UserEnterExclusive();

    /* Get the Thread */
    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_SET_INFORMATION,
                                       *PsThreadType,
                                       UserMode,
                                       (PVOID)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) goto Quit;

    switch (ThreadInformationClass)
    {
        case UserThreadInitiateShutdown:
        {
            ULONG CapturedFlags = 0;

            TRACE("Shutdown initiated\n");

            if (ThreadInformationLength != sizeof(CapturedFlags))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            /* Capture the caller value */
            Status = STATUS_SUCCESS;
            _SEH2_TRY
            {
                ProbeForWrite(ThreadInformation, sizeof(CapturedFlags), __alignof(CapturedFlags));
                CapturedFlags = *(PULONG)ThreadInformation;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
                _SEH2_YIELD(break);
            }
            _SEH2_END;

            Status = UserInitiateShutdown(Thread, &CapturedFlags);

            /* Return the modified value to the caller */
            _SEH2_TRY
            {
                *(PULONG)ThreadInformation = CapturedFlags;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        case UserThreadEndShutdown:
        {
            NTSTATUS ShutdownStatus;

            TRACE("Shutdown ended\n");

            if (ThreadInformationLength != sizeof(ShutdownStatus))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            /* Capture the caller value */
            Status = STATUS_SUCCESS;
            _SEH2_TRY
            {
                ProbeForRead(ThreadInformation, sizeof(ShutdownStatus), __alignof(ShutdownStatus));
                ShutdownStatus = *(NTSTATUS*)ThreadInformation;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
                _SEH2_YIELD(break);
            }
            _SEH2_END;

            Status = UserEndShutdown(Thread, ShutdownStatus);
            break;
        }

        case UserThreadCsrApiPort:
        {
            HANDLE CsrPortHandle;


            TRACE("Set CSR API Port for Win32k\n");
            if (ThreadInformationLength != sizeof(CsrPortHandle))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            /* Capture the caller value */
            Status = STATUS_SUCCESS;
            _SEH2_TRY
            {
                ProbeForRead(ThreadInformation, sizeof(CsrPortHandle), __alignof(CsrPortHandle));
                CsrPortHandle = *(PHANDLE)ThreadInformation;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
                _SEH2_YIELD(break);
            }
            _SEH2_END;

            Status = InitCsrApiPort(CsrPortHandle);
            break;
        }

        case UserThreadUseActiveDesktop:
        {
            HDESK hdesk;

            if (Thread != PsGetCurrentThread())
            {
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            hdesk = IntGetDesktopObjectHandle(gpdeskInputDesktop);
            IntSetThreadDesktop(hdesk, FALSE);

            break;
        }
        case UserThreadRestoreDesktop:
        {
            if (Thread != PsGetCurrentThread())
            {
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            IntSetThreadDesktop(NULL, FALSE);
            break;
        }
        default:
        {
            STUB;
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }
    }

    ObDereferenceObject(Thread);

Quit:
    UserLeave();
    return Status;
}

BOOL
APIENTRY
NtUserSoundSentry(VOID)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserTestForInteractiveUser(
    DWORD dwUnknown1)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserRemoteConnect(
    DWORD dwUnknown1,
    DWORD dwUnknown2,
    DWORD dwUnknown3)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserRemoteRedrawRectangle(
    DWORD dwUnknown1,
    DWORD dwUnknown2,
    DWORD dwUnknown3,
    DWORD dwUnknown4)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserRemoteRedrawScreen(VOID)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserRemoteStopScreenUpdates(VOID)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtUserCtxDisplayIOCtl(
    DWORD dwUnknown1,
    DWORD dwUnknown2,
    DWORD dwUnknown3)
{
    STUB;
    return 0;
}

DWORD APIENTRY
NtUserQuerySendMessage(DWORD Unknown0)
{
    STUB;

    return 0;
}

BOOL APIENTRY NtUserAddClipboardFormatListener(
    HWND hwnd
)
{
    STUB;
    return FALSE;
}

BOOL APIENTRY NtUserRemoveClipboardFormatListener(
    HWND hwnd
)
{
    STUB;
    return FALSE;
}

BOOL APIENTRY NtUserGetUpdatedClipboardFormats(
    PUINT lpuiFormats,
    UINT cFormats,
    PUINT pcFormatsOut
)
{
    STUB;
    return FALSE;
}

// Yes, I know, these do not belong here, just tell me where to put them
BOOL
APIENTRY
NtGdiMakeObjectXferable(
    _In_ HANDLE hHandle,
    _In_ DWORD dwProcessId)
{
    STUB;
    return 0;
}

BOOL
APIENTRY
NtGdiMakeObjectUnXferable(
    _In_ HANDLE hHandle)
{
    STUB;
    return 0;
}

DWORD
APIENTRY
NtDxEngGetRedirectionBitmap(
    DWORD Unknown0)
{
    STUB;
    return 0;
}

/*
 * Win8+ services surfaced through win32u. Semantics follow the Wine win32u
 * pair; parity is validated by the synced win32u winetest.
 */

typedef struct _NTUSER_DISPLAYCONFIG_DEVICE_INFO_HEADER
{
    ULONG Type;
    ULONG Size;
    LUID AdapterId;
    ULONG Id;
} NTUSER_DISPLAYCONFIG_DEVICE_INFO_HEADER;

LONG
APIENTRY
NtUserDisplayConfigGetDeviceInfo(
    _Inout_ PVOID pPacket)
{
    NTUSER_DISPLAYCONFIG_DEVICE_INFO_HEADER Header;

    _SEH2_TRY
    {
        ProbeForRead(pPacket, sizeof(Header), sizeof(ULONG));
        RtlCopyMemory(&Header, pPacket, sizeof(Header));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
    }
    _SEH2_END;

    /* Every request type carries a payload beyond the bare header */
    if (Header.Size <= sizeof(Header))
        return STATUS_INVALID_PARAMETER;

    /* No display configuration topology support yet */
    return STATUS_NOT_SUPPORTED;
}

BOOL
APIENTRY
NtUserEnableMouseInPointer(
    _In_ BOOL fEnable)
{
    PPROCESSINFO ppi;
    BOOL Ret = FALSE;

    UserEnterExclusive();

    ppi = PsGetCurrentProcessWin32Process();
    if (ppi->MouseInPointerSet && ppi->MouseInPointerEnabled != !!fEnable)
    {
        /* The choice is process-lifetime one-shot, like on Windows */
        EngSetLastError(ERROR_ACCESS_DENIED);
    }
    else
    {
        ppi->MouseInPointerEnabled = !!fEnable;
        ppi->MouseInPointerSet = TRUE;
        Ret = TRUE;
    }

    UserLeave();
    return Ret;
}

BOOL
APIENTRY
NtUserIsMouseInPointerEnabled(VOID)
{
    PPROCESSINFO ppi;
    BOOL Ret;

    UserEnterShared();
    ppi = PsGetCurrentProcessWin32Process();
    Ret = ppi->MouseInPointerEnabled;
    UserLeave();

    return Ret;
}

BOOL
APIENTRY
NtUserGetPointerDeviceRects(
    _In_opt_ HANDLE hDevice,
    _Out_ PRECT prcPointerDevice,
    _Out_ PRECT prcDisplay)
{
    RECT rcScreen;

    UNREFERENCED_PARAMETER(hDevice);

    UserEnterShared();
    rcScreen.left = UserGetSystemMetrics(SM_XVIRTUALSCREEN);
    rcScreen.top = UserGetSystemMetrics(SM_YVIRTUALSCREEN);
    rcScreen.right = rcScreen.left + UserGetSystemMetrics(SM_CXVIRTUALSCREEN);
    rcScreen.bottom = rcScreen.top + UserGetSystemMetrics(SM_CYVIRTUALSCREEN);
    UserLeave();

    _SEH2_TRY
    {
        ProbeForWrite(prcPointerDevice, sizeof(RECT), sizeof(ULONG));
        *prcPointerDevice = rcScreen;
        ProbeForWrite(prcDisplay, sizeof(RECT), sizeof(ULONG));
        *prcDisplay = rcScreen;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        EngSetLastError(ERROR_NOACCESS);
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    return TRUE;
}

BOOL
APIENTRY
NtUserGetPointerInfoList(
    _In_ ULONG PointerId,
    _In_ ULONG PointerType,
    _In_ ULONG_PTR SourceDevice,
    _In_ ULONG_PTR hProcess,
    _In_ ULONG_PTR EntrySize,
    _Inout_ PULONG EntriesCount,
    _Inout_ PULONG PointerCount,
    _Out_opt_ PVOID PointerInfo)
{
    UNREFERENCED_PARAMETER(PointerId);
    UNREFERENCED_PARAMETER(PointerType);
    UNREFERENCED_PARAMETER(SourceDevice);
    UNREFERENCED_PARAMETER(hProcess);

    _SEH2_TRY
    {
        volatile ULONG *Count;

        ProbeForWrite(EntriesCount, sizeof(ULONG), 1);
        Count = (volatile ULONG *)EntriesCount;
        *Count = *Count;
        ProbeForWrite(PointerCount, sizeof(ULONG), 1);
        Count = (volatile ULONG *)PointerCount;
        *Count = *Count;
        if (PointerInfo && EntrySize)
            ProbeForWrite(PointerInfo, EntrySize, 1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        EngSetLastError(ERROR_NOACCESS);
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    /* No pointer input frame history is recorded yet */
    EngSetLastError(ERROR_NO_DATA);
    return FALSE;
}

static BOOL
IntIsValidDpiContext(_In_ ULONG Context, _In_ ULONG SystemDpi)
{
    switch (NTUSER_DPI_CONTEXT_GET_AWARENESS(Context))
    {
        case DPI_AWARENESS_UNAWARE:
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & ~NTUSER_DPI_CONTEXT_FLAG_VALID_MASK)
            {
                return FALSE;
            }
            if (NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 1)
                return FALSE;
            return NTUSER_DPI_CONTEXT_GET_DPI(Context) == 96;

        case DPI_AWARENESS_SYSTEM_AWARE:
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & ~NTUSER_DPI_CONTEXT_FLAG_VALID_MASK)
            {
                return FALSE;
            }
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & NTUSER_DPI_CONTEXT_FLAG_GDISCALED)
            {
                return FALSE;
            }
            if (NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 1)
                return FALSE;
            return !SystemDpi || NTUSER_DPI_CONTEXT_GET_DPI(Context) == SystemDpi;

        case DPI_AWARENESS_PER_MONITOR_AWARE:
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & ~NTUSER_DPI_CONTEXT_FLAG_VALID_MASK)
            {
                return FALSE;
            }
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & NTUSER_DPI_CONTEXT_FLAG_GDISCALED)
            {
                return FALSE;
            }
            if (NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 1 && NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 2)
            {
                return FALSE;
            }
            return NTUSER_DPI_CONTEXT_GET_DPI(Context) == 0;
    }

    return FALSE;
}

ULONG
APIENTRY
NtUserGetProcessDpiAwarenessContext(
    _In_ HANDLE hProcess)
{
    ULONG Ret = NTUSER_DPI_UNAWARE;
    PPROCESSINFO ppi;

    UserEnterShared();

    if (hProcess == NtCurrentProcess())
    {
        ppi = PsGetCurrentProcessWin32Process();
        if (ppi && ppi->DpiContext)
            Ret = ppi->DpiContext;
    }
    else
    {
        PEPROCESS Process;
        NTSTATUS Status;

        Status = ObReferenceObjectByHandle(hProcess,
                                           PROCESS_QUERY_INFORMATION,
                                           *PsProcessType,
                                           UserMode,
                                           (PVOID *)&Process,
                                           NULL);
        if (NT_SUCCESS(Status))
        {
            ppi = PsGetProcessWin32Process(Process);
            if (ppi && ppi->DpiContext)
                Ret = ppi->DpiContext;
            ObDereferenceObject(Process);
        }
        else
        {
            EngSetLastError(ERROR_INVALID_HANDLE);
            Ret = 0;
        }
    }

    UserLeave();
    return Ret;
}

ULONG
APIENTRY
NtUserGetThreadDpiAwarenessContext(VOID)
{
    PTHREADINFO pti;
    ULONG Context = NTUSER_DPI_UNAWARE;

    UserEnterShared();

    pti = PsGetCurrentThreadWin32Thread();
    if (pti)
    {
        if (pti->DpiContext)
        {
            Context = pti->DpiContext;
        }
        else if (pti->ppi && pti->ppi->DpiContext)
        {
            Context = pti->ppi->DpiContext;
        }
    }

    UserLeave();
    return Context;
}

ULONG
APIENTRY
NtUserGetWindowDpiAwarenessContext(_In_ HWND hWnd)
{
    PWND Window;
    ULONG Context = 0;

    UserEnterShared();

    Window = UserGetWindowObject(hWnd);
    if (Window)
        Context = Window->DpiContext;

    UserLeave();
    return Context;
}

ULONG
APIENTRY
NtUserSetProcessDpiAwarenessContext(
    _In_ ULONG DpiContext,
    _In_ ULONG Flags)
{
    PPROCESSINFO ppi;
    ULONG SystemDpi;
    ULONG Ret = 0;

    UNREFERENCED_PARAMETER(Flags);

    UserEnterExclusive();

    SystemDpi = (gpsi && gpsi->dmLogPixels) ? gpsi->dmLogPixels : 96;

    /* Only valid concrete NTUSER contexts are accepted, never abstract handles. */
    if (!IntIsValidDpiContext(DpiContext, SystemDpi))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
    }
    else
    {
        ppi = PsGetCurrentProcessWin32Process();
        if (!ppi)
        {
            EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
        }
        else if (!ppi->DpiContext)
        {
            /* Process-lifetime one-shot, like on Windows */
            ppi->DpiContext = DpiContext;
            Ret = 1;
        }
        else
        {
            EngSetLastError(ERROR_ACCESS_DENIED);
        }
    }

    UserLeave();
    return Ret;
}

ULONG
APIENTRY
NtUserSetThreadDpiAwarenessContext(_In_ ULONG DpiContext)
{
    PTHREADINFO pti;
    ULONG SystemDpi;
    ULONG Previous = 0;

    UserEnterExclusive();

    pti = PsGetCurrentThreadWin32Thread();
    SystemDpi = (gpsi && gpsi->dmLogPixels) ? gpsi->dmLogPixels : 96;

    if (!IntIsValidDpiContext(DpiContext, SystemDpi))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
    }
    else if (!pti)
    {
        EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    else
    {
        if (pti->DpiContext)
        {
            Previous = pti->DpiContext;
        }
        else if (pti->ppi && pti->ppi->DpiContext)
        {
            Previous = pti->ppi->DpiContext | NTUSER_DPI_CONTEXT_FLAG_PROCESS;
        }
        else
        {
            Previous = NTUSER_DPI_UNAWARE | NTUSER_DPI_CONTEXT_FLAG_PROCESS;
        }

        if (DpiContext & NTUSER_DPI_CONTEXT_FLAG_PROCESS)
            pti->DpiContext = 0;
        else
            pti->DpiContext = DpiContext;
    }

    UserLeave();
    return Previous;
}

/* EOF */
