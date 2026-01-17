/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel
 * FILE:            ntoskrnl/ex/win32k.c
 * PURPOSE:         Executive Win32 Object Support (Desktop/WinStation)
 * PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct _WIN32_KERNEL_OBJECT_HEADER
{
    ULONG SessionId;
} WIN32_KERNEL_OBJECT_HEADER, *PWIN32_KERNEL_OBJECT_HEADER;


/* DATA **********************************************************************/

POBJECT_TYPE ExWindowStationObjectType = NULL;
POBJECT_TYPE ExDesktopObjectType = NULL;

GENERIC_MAPPING ExpWindowStationMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_REQUIRED
};

GENERIC_MAPPING ExpDesktopMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_REQUIRED
};

PKWIN32_SESSION_CALLOUT ExpWindowStationObjectParse = NULL;
PKWIN32_SESSION_CALLOUT ExpWindowStationObjectDelete = NULL;
PKWIN32_SESSION_CALLOUT ExpWindowStationObjectOkToClose = NULL;
PKWIN32_SESSION_CALLOUT ExpDesktopObjectOkToClose = NULL;
PKWIN32_SESSION_CALLOUT ExpDesktopObjectDelete = NULL;
PKWIN32_SESSION_CALLOUT ExpDesktopObjectOpen = NULL;
PKWIN32_SESSION_CALLOUT ExpDesktopObjectClose = NULL;

/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
ExpWin32SessionCallout(
    _In_ PVOID Object,
    _In_ PKWIN32_SESSION_CALLOUT CalloutProcedure,
    _Inout_opt_ PVOID Parameter)
{
    PWIN32_KERNEL_OBJECT_HEADER Win32ObjectHeader;
    PVOID SessionEntry = NULL;
    KAPC_STATE ApcState;
    NTSTATUS Status;

    /* If Win32k.sys is not loaded yet, the callout procedure will be NULL.
     * This can happen during early boot when creating Win32 object types.
     * Return success to allow object type creation to proceed. */
    if (CalloutProcedure == NULL)
    {
        DPRINT("ExpWin32SessionCallout: CalloutProcedure is NULL (Win32k not loaded)\n");
        return STATUS_SUCCESS;
    }

    /* The objects have a common header. And the kernel accesses it!
       Thanks MS for this kind of retarded "design"! */
    Win32ObjectHeader = Object;

    /* Check if we are not already in the correct session */
    if (!PsGetCurrentProcess()->ProcessInSession ||
        (PsGetCurrentProcessSessionId() != Win32ObjectHeader->SessionId))
    {
        /* Get the session from the objects session Id */
        DPRINT("SessionId == %d\n", Win32ObjectHeader->SessionId);
        SessionEntry = MmGetSessionById(Win32ObjectHeader->SessionId);
        if (SessionEntry == NULL)
        {
            /* The requested session does not even exist! */
            ASSERT(FALSE);
            return STATUS_NOT_FOUND;
        }

        /* Attach to the session */
        Status = MmAttachSession(SessionEntry, &ApcState);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Could not attach to 0x%p, object %p, callout 0x%p\n",
                    SessionEntry,
                    Win32ObjectHeader,
                    CalloutProcedure);

            /* Cleanup and return */
            MmQuitNextSession(SessionEntry);
            ASSERT(FALSE);
            return Status;
        }
    }

    /* Call the callout routine */
    Status = CalloutProcedure(Parameter);

    /* Check if we have a session */
    if (SessionEntry != NULL)
    {
        /* Detach from the session and quit using it */
        MmDetachSession(SessionEntry, &ApcState);
        MmQuitNextSession(SessionEntry);
    }

    /* Return the callback status */
    return Status;
}

BOOLEAN
NTAPI
ExpDesktopOkToClose( IN PEPROCESS Process OPTIONAL,
                     IN PVOID Object,
                     IN HANDLE Handle,
                     IN KPROCESSOR_MODE AccessMode)
{
    WIN32_OKAYTOCLOSEMETHOD_PARAMETERS Parameters;
    NTSTATUS Status;

    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.Handle = Handle;
    Parameters.PreviousMode = AccessMode;

    Status = ExpWin32SessionCallout(Object,
                                    ExpDesktopObjectOkToClose,
                                    &Parameters);

    return NT_SUCCESS(Status);
}

BOOLEAN
NTAPI
ExpWindowStationOkToClose( IN PEPROCESS Process OPTIONAL,
                     IN PVOID Object,
                     IN HANDLE Handle,
                     IN KPROCESSOR_MODE AccessMode)
{
    WIN32_OKAYTOCLOSEMETHOD_PARAMETERS Parameters;
    NTSTATUS Status;

    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.Handle = Handle;
    Parameters.PreviousMode = AccessMode;

    Status = ExpWin32SessionCallout(Object,
                                    ExpWindowStationObjectOkToClose,
                                    &Parameters);

    return NT_SUCCESS(Status);
}

VOID
NTAPI
ExpWinStaObjectDelete(PVOID DeletedObject)
{
    WIN32_DELETEMETHOD_PARAMETERS Parameters;

    /* Fill out the callback structure */
    Parameters.Object = DeletedObject;

    ExpWin32SessionCallout(DeletedObject,
                           ExpWindowStationObjectDelete,
                           &Parameters);
}

NTSTATUS
NTAPI
ExpWinStaObjectParse(IN PVOID ParseObject,
                     IN PVOID ObjectType,
                     IN OUT PACCESS_STATE AccessState,
                     IN KPROCESSOR_MODE AccessMode,
                     IN ULONG Attributes,
                     IN OUT PUNICODE_STRING CompleteName,
                     IN OUT PUNICODE_STRING RemainingName,
                     IN OUT PVOID Context OPTIONAL,
                     IN PSECURITY_QUALITY_OF_SERVICE SecurityQos OPTIONAL,
                     OUT PVOID *Object)
{
    WIN32_PARSEMETHOD_PARAMETERS Parameters;

    /* Fill out the callback structure */
    Parameters.ParseObject = ParseObject;
    Parameters.ObjectType = ObjectType;
    Parameters.AccessState = AccessState;
    Parameters.AccessMode = AccessMode;
    Parameters.Attributes = Attributes;
    Parameters.CompleteName = CompleteName;
    Parameters.RemainingName = RemainingName;
    Parameters.Context = Context;
    Parameters.SecurityQos = SecurityQos;
    Parameters.Object = Object;

    return ExpWin32SessionCallout(ParseObject,
                                  ExpWindowStationObjectParse,
                                  &Parameters);
}
VOID
NTAPI
ExpDesktopDelete(PVOID DeletedObject)
{
    WIN32_DELETEMETHOD_PARAMETERS Parameters;

    /* Fill out the callback structure */
    Parameters.Object = DeletedObject;

    ExpWin32SessionCallout(DeletedObject,
                           ExpDesktopObjectDelete,
                           &Parameters);
}

NTSTATUS
NTAPI
ExpDesktopOpen(IN OB_OPEN_REASON Reason,
               IN PEPROCESS Process OPTIONAL,
               IN PVOID ObjectBody,
               IN ACCESS_MASK GrantedAccess,
               IN ULONG HandleCount)
{
    WIN32_OPENMETHOD_PARAMETERS Parameters;

    Parameters.OpenReason = Reason;
    Parameters.Process = Process;
    Parameters.Object = ObjectBody;
    Parameters.GrantedAccess = GrantedAccess;
    Parameters.HandleCount = HandleCount;

    return ExpWin32SessionCallout(ObjectBody,
                                  ExpDesktopObjectOpen,
                                  &Parameters);
}

VOID
NTAPI
ExpDesktopClose(IN PEPROCESS Process OPTIONAL,
                IN PVOID Object,
                IN ACCESS_MASK GrantedAccess,
                IN ULONG ProcessHandleCount,
                IN ULONG SystemHandleCount)
{
    WIN32_CLOSEMETHOD_PARAMETERS Parameters;

    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.AccessMask = GrantedAccess;
    Parameters.ProcessHandleCount = ProcessHandleCount;
    Parameters.SystemHandleCount = SystemHandleCount;

    ExpWin32SessionCallout(Object,
                           ExpDesktopObjectClose,
                           &Parameters);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpWin32kInit(VOID)
{
    /*
     * Win32k Object Type Initialization Strategy:
     *
     * This function creates the WindowStation and Desktop object TYPES during
     * Phase 1 kernel initialization. This is intentional and correct behavior:
     *
     * 1. Object types are created NOW (Phase 1), before win32k.sys loads
     * 2. The callback pointers (ExpDesktopObjectOpen, etc.) are initially NULL
     * 3. ExpWin32SessionCallout() handles NULL callbacks gracefully (returns success)
     * 4. Later, when win32k.sys loads via DriverEntry(), it calls:
     *    - PsEstablishWin32Callouts() to fill in the callback pointers
     *    - InitWindowStationImpl() and InitDesktopImpl() to configure type properties
     *
     * This design separates object TYPE creation (kernel's job) from object type
     * CONFIGURATION and actual object creation (win32k's job when sessions start).
     *
     * ARM64 Note: The previous ARM64-specific skip was removed because by Phase 1,
     * MmInitSystem(1, ...) has already expanded pool memory, providing sufficient
     * resources even with 64KB pages. The object types themselves are lightweight
     * metadata structures, not large allocations.
     */
    UNICODE_STRING Name;
    NTSTATUS Status;

    /*
     * ARM64 FIX: Use separate OBJECT_TYPE_INITIALIZER variables to avoid
     * compiler bug where structure pointer is offset by 8 bytes on reuse.
     * See ARM64_STRUCTURE_POINTER_OFFSET_BUG.md for details.
     */

    /* Create the window station Object Type */
    {
        OBJECT_TYPE_INITIALIZER WinStaTypeInitializer;
        RtlZeroMemory(&WinStaTypeInitializer, sizeof(WinStaTypeInitializer));

        RtlInitUnicodeString(&Name, L"WindowStation");
        WinStaTypeInitializer.Length = sizeof(WinStaTypeInitializer);
        WinStaTypeInitializer.GenericMapping = ExpWindowStationMapping;
        WinStaTypeInitializer.PoolType = NonPagedPool;
        WinStaTypeInitializer.DeleteProcedure = ExpWinStaObjectDelete;
        WinStaTypeInitializer.ParseProcedure = ExpWinStaObjectParse;
        WinStaTypeInitializer.OkayToCloseProcedure = ExpWindowStationOkToClose;
        WinStaTypeInitializer.SecurityRequired = TRUE;
        WinStaTypeInitializer.InvalidAttributes = OBJ_OPENLINK |
                                                  OBJ_PERMANENT |
                                                  OBJ_EXCLUSIVE;
        WinStaTypeInitializer.ValidAccessMask = STANDARD_RIGHTS_REQUIRED;

        Status = ObCreateObjectType(&Name,
                                    &WinStaTypeInitializer,
                                    NULL,
                                    &ExWindowStationObjectType);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ExpWin32kInit: Failed to create WindowStation object type - Status=0x%08lx\n", Status);
            return FALSE;
        }
    }

    /* Create desktop object type */
    {
        OBJECT_TYPE_INITIALIZER DesktopTypeInitializer;
        RtlZeroMemory(&DesktopTypeInitializer, sizeof(DesktopTypeInitializer));

        RtlInitUnicodeString(&Name, L"Desktop");
        DesktopTypeInitializer.Length = sizeof(DesktopTypeInitializer);
        DesktopTypeInitializer.GenericMapping = ExpDesktopMapping;
        DesktopTypeInitializer.PoolType = NonPagedPool;
        DesktopTypeInitializer.DeleteProcedure = ExpDesktopDelete;
        DesktopTypeInitializer.ParseProcedure = NULL;
        DesktopTypeInitializer.OkayToCloseProcedure = ExpDesktopOkToClose;
        DesktopTypeInitializer.OpenProcedure = ExpDesktopOpen;
        DesktopTypeInitializer.CloseProcedure = ExpDesktopClose;
        DesktopTypeInitializer.SecurityRequired = TRUE;
        DesktopTypeInitializer.InvalidAttributes = OBJ_OPENLINK |
                                                  OBJ_PERMANENT |
                                                  OBJ_EXCLUSIVE;
        DesktopTypeInitializer.ValidAccessMask = STANDARD_RIGHTS_REQUIRED;

        Status = ObCreateObjectType(&Name,
                                    &DesktopTypeInitializer,
                                    NULL,
                                    &ExDesktopObjectType);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ExpWin32kInit: Failed to create Desktop object type - Status=0x%08lx\n", Status);
            return FALSE;
        }
    }

    return TRUE;
}

/* EOF */
