/*
 *
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/kernel32/client/power.c
 * PURPOSE:         Power Management Functions
 * PROGRAMMER:      Dmitry Chapyshev <dmitry@reactos.org>
 *
 * UPDATE HISTORY:
 *                  01/15/2009 Created
 */

#include <k32.h>

#include <ndk/pofuncs.h>

#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
GetSystemPowerStatus(IN LPSYSTEM_POWER_STATUS PowerStatus)
{
    NTSTATUS Status;
    SYSTEM_BATTERY_STATE BattState;
    ULONG Max, Current;

    Status = NtPowerInformation(SystemBatteryState,
                                NULL,
                                0,
                                &BattState,
                                sizeof(SYSTEM_BATTERY_STATE));
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    RtlZeroMemory(PowerStatus, sizeof(SYSTEM_POWER_STATUS));

    PowerStatus->BatteryLifeTime = BATTERY_LIFE_UNKNOWN;
    PowerStatus->BatteryFullLifeTime = BATTERY_LIFE_UNKNOWN;
    PowerStatus->BatteryLifePercent = BATTERY_PERCENTAGE_UNKNOWN;
    PowerStatus->ACLineStatus = AC_LINE_ONLINE;

    Max = BattState.MaxCapacity;
    Current = BattState.RemainingCapacity;
    if (Max)
    {
        if (Current <= Max)
        {
            PowerStatus->BatteryLifePercent = (UCHAR)((100 * Current + Max / 2) / Max);
        }
        else
        {
            PowerStatus->BatteryLifePercent = 100;
        }

        if (PowerStatus->BatteryLifePercent <= 4)
            PowerStatus->BatteryFlag |= BATTERY_FLAG_CRITICAL;

        if (PowerStatus->BatteryLifePercent <= 32)
            PowerStatus->BatteryFlag |= BATTERY_FLAG_LOW;

        if (PowerStatus->BatteryLifePercent >= 67)
            PowerStatus->BatteryFlag |= BATTERY_FLAG_HIGH;
    }

    if (!BattState.BatteryPresent)
        PowerStatus->BatteryFlag |= BATTERY_FLAG_NO_BATTERY;

    if (BattState.Charging)
        PowerStatus->BatteryFlag |= BATTERY_FLAG_CHARGING;

    if (!(BattState.AcOnLine) && (BattState.BatteryPresent))
        PowerStatus->ACLineStatus = AC_LINE_OFFLINE;

    if (BattState.EstimatedTime)
        PowerStatus->BatteryLifeTime = BattState.EstimatedTime;

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetSystemPowerState(IN BOOL fSuspend,
                    IN BOOL fForce)
{
    NTSTATUS Status;

    Status = NtInitiatePowerAction((fSuspend != FALSE) ? PowerActionSleep     : PowerActionHibernate,
                                   (fSuspend != FALSE) ? PowerSystemSleeping1 : PowerSystemHibernate,
                                   (fForce == FALSE),
                                   FALSE);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetDevicePowerState(IN HANDLE hDevice,
                    OUT BOOL *pfOn)
{
    DEVICE_POWER_STATE DevicePowerState;
    NTSTATUS Status;

    Status = NtGetDevicePowerState(hDevice, &DevicePowerState);
    if (NT_SUCCESS(Status))
    {
        *pfOn = (DevicePowerState == PowerDeviceUnspecified) ||
                (DevicePowerState == PowerDeviceD0);
        return TRUE;
    }

    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
WINAPI
RequestDeviceWakeup(IN HANDLE hDevice)
{
    NTSTATUS Status;

    Status = NtRequestDeviceWakeup(hDevice);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
RequestWakeupLatency(IN LATENCY_TIME latency)
{
    NTSTATUS Status;

    Status = NtRequestWakeupLatency(latency);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
CancelDeviceWakeupRequest(IN HANDLE hDevice)
{
    NTSTATUS Status;

    Status = NtCancelDeviceWakeupRequest(hDevice);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
IsSystemResumeAutomatic(VOID)
{
    return (BOOL)NtIsSystemResumeAutomatic();
}

/*
 * @implemented
 */
BOOL
WINAPI
SetMessageWaitingIndicator(IN HANDLE hMsgIndicator,
                           IN ULONG ulMsgCount)
{
    /* This is the correct Windows implementation */
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

/*
 * @implemented
 */
EXECUTION_STATE
WINAPI
SetThreadExecutionState(EXECUTION_STATE esFlags)
{
    NTSTATUS Status;

    Status = NtSetThreadExecutionState(esFlags, &esFlags);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return 0;
    }

    return esFlags;
}

typedef struct _BASE_POWER_REQUEST_ACTION
{
    HANDLE PowerRequestHandle;
    POWER_REQUEST_TYPE RequestType;
    ULONG SetAction;
    PVOID Reserved;
} BASE_POWER_REQUEST_ACTION, *PBASE_POWER_REQUEST_ACTION;

typedef struct _BASE_COUNTED_REASON_CONTEXT
{
    ULONG Version;
    ULONG Flags;
    union
    {
        struct
        {
            UNICODE_STRING ResourceFileName;
            USHORT ResourceReasonId;
            ULONG StringCount;
            PUNICODE_STRING ReasonStrings;
        };
        UNICODE_STRING SimpleString;
    };
} BASE_COUNTED_REASON_CONTEXT, *PBASE_COUNTED_REASON_CONTEXT;

/*
 * @implemented
 */
HANDLE
WINAPI
PowerCreateRequest(
    _In_opt_ PREASON_CONTEXT Context)
{
    BASE_COUNTED_REASON_CONTEXT CountedContext;
    PUNICODE_STRING ReasonStrings = NULL;
    WCHAR ModulePath[MAX_PATH];
    HANDLE Handle;
    NTSTATUS Status;
    ULONG i;

    RtlZeroMemory(&CountedContext, sizeof(CountedContext));
    CountedContext.Version = POWER_REQUEST_CONTEXT_VERSION;

    if (!Context)
    {
        CountedContext.Flags = DIAGNOSTIC_REASON_NOT_SPECIFIED;
    }
    else if (Context->Version != POWER_REQUEST_CONTEXT_VERSION)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    else if (Context->Flags == POWER_REQUEST_CONTEXT_SIMPLE_STRING)
    {
        if (!Context->Reason.SimpleReasonString || !*Context->Reason.SimpleReasonString)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_HANDLE_VALUE;
        }
        CountedContext.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        RtlInitUnicodeString(&CountedContext.SimpleString, Context->Reason.SimpleReasonString);
    }
    else if (Context->Flags == POWER_REQUEST_CONTEXT_DETAILED_STRING)
    {
        if (!GetModuleFileNameW(Context->Reason.Detailed.LocalizedReasonModule, ModulePath, ARRAYSIZE(ModulePath))) return INVALID_HANDLE_VALUE;
        if (Context->Reason.Detailed.ReasonStringCount > MAXULONG / sizeof(*ReasonStrings))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_HANDLE_VALUE;
        }

        CountedContext.Flags = POWER_REQUEST_CONTEXT_DETAILED_STRING;
        RtlInitUnicodeString(&CountedContext.ResourceFileName, ModulePath);
        CountedContext.ResourceReasonId = (USHORT)Context->Reason.Detailed.LocalizedReasonId;
        CountedContext.StringCount = Context->Reason.Detailed.ReasonStringCount;
        if (CountedContext.StringCount)
        {
            ReasonStrings = RtlAllocateHeap(RtlGetProcessHeap(), 0, CountedContext.StringCount * sizeof(*ReasonStrings));
            if (!ReasonStrings)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return INVALID_HANDLE_VALUE;
            }

            for (i = 0; i < CountedContext.StringCount; i++) RtlInitUnicodeString(&ReasonStrings[i], Context->Reason.Detailed.ReasonStrings[i]);
            CountedContext.ReasonStrings = ReasonStrings;
        }
    }
    else
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    Status = NtPowerInformation(PowerRequestCreate, &CountedContext, sizeof(CountedContext), &Handle, sizeof(Handle));
    if (ReasonStrings) RtlFreeHeap(RtlGetProcessHeap(), 0, ReasonStrings);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return INVALID_HANDLE_VALUE;
    }

    if (Context && Context->Flags == POWER_REQUEST_CONTEXT_DETAILED_STRING) SetLastError(ERROR_SUCCESS);
    return Handle;
}

/*
 * @implemented
 */
BOOL
WINAPI
PowerSetRequest(
    _In_ HANDLE PowerRequest,
    _In_ POWER_REQUEST_TYPE RequestType)
{
    BASE_POWER_REQUEST_ACTION Action;
    NTSTATUS Status;

    Action.PowerRequestHandle = PowerRequest;
    Action.RequestType = RequestType;
    Action.SetAction = TRUE;
    Action.Reserved = NULL;
    Status = NtPowerInformation(PowerRequestAction, &Action, sizeof(Action), NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
PowerClearRequest(
    _In_ HANDLE PowerRequest,
    _In_ POWER_REQUEST_TYPE RequestType)
{
    BASE_POWER_REQUEST_ACTION Action;
    NTSTATUS Status;

    Action.PowerRequestHandle = PowerRequest;
    Action.RequestType = RequestType;
    Action.SetAction = FALSE;
    Action.Reserved = NULL;
    Status = NtPowerInformation(PowerRequestAction, &Action, sizeof(Action), NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
