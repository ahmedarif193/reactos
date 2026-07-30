#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ntverp.h>

extern "C" {
#include <ntddk.h>
#include <ntstrsafe.h>
}

#include <wdf.h>

#define  FX_DYNAMICS_GENERATE_TABLE   1

//-----------------------------------------    ------------------------------------

extern "C" {

typedef VOID (NTAPI *WDFFUNC) (VOID);
#define  KMDF_DEFAULT_NAME   "Wdf01000"

void
__cxa_pure_virtual()
{
	__debugbreak();
}

VOID
NTAPI
imp_WdfDmaTransactionSetSingleTransferRequirement(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDMATRANSACTION DmaTransaction,
    _In_ BOOLEAN RequireSingleTransfer
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(DmaTransaction);
    UNREFERENCED_PARAMETER(RequireSingleTransfer);
}

ULONG
NTAPI
imp_WdfFileObjectGetInitiatorProcessId(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFFILEOBJECT FileObject
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(FileObject);
    return 0;
}

NTSTATUS
NTAPI
imp_WdfDeviceRetrieveCompanionTarget(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE Device,
    _Out_ WDFCOMPANIONTARGET* CompanionTarget
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(Device);

    if (CompanionTarget != NULL) {
        *CompanionTarget = NULL;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
imp_WdfCompanionTargetSendTaskSynchronously(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFCOMPANIONTARGET CompanionTarget,
    _In_ USHORT TaskQueueIdentifier,
    _In_ ULONG TaskOperationCode,
    _In_opt_ PWDF_MEMORY_DESCRIPTOR InputBuffer,
    _In_opt_ PWDF_MEMORY_DESCRIPTOR OutputBuffer,
    _In_opt_ PWDF_TASK_SEND_OPTIONS TaskOptions,
    _Out_ PULONG_PTR BytesReturned
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(CompanionTarget);
    UNREFERENCED_PARAMETER(TaskQueueIdentifier);
    UNREFERENCED_PARAMETER(TaskOperationCode);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(TaskOptions);

    if (BytesReturned != NULL) {
        *BytesReturned = 0;
    }

    return STATUS_NOT_SUPPORTED;
}

PEPROCESS
NTAPI
imp_WdfCompanionTargetWdmGetCompanionProcess(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFCOMPANIONTARGET CompanionTarget
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(CompanionTarget);
    return NULL;
}

}  // extern "C"
