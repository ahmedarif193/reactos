/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         User APC and I/O completion reserve objects
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

POBJECT_TYPE ExUserApcReserveObjectType;
POBJECT_TYPE ExIoCompletionReserveObjectType;

static GENERIC_MAPPING ExpReserveMapping =
{
    STANDARD_RIGHTS_READ | 0x1,
    STANDARD_RIGHTS_WRITE | 0x2,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_REQUIRED | 0x3
};

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeReserveObjectTypes(VOID)
{
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    UNICODE_STRING Name;
    NTSTATUS Status;

    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(EX_RESERVE_OBJECT);
    ObjectTypeInitializer.GenericMapping = ExpReserveMapping;
    ObjectTypeInitializer.PoolType = NonPagedPool;
    ObjectTypeInitializer.ValidAccessMask = STANDARD_RIGHTS_REQUIRED | 0x3;
    ObjectTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
    ObjectTypeInitializer.UseDefaultObject = TRUE;

    RtlInitUnicodeString(&Name, L"UserApcReserve");
    Status = ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ExUserApcReserveObjectType);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlInitUnicodeString(&Name, L"IoCompletionReserve");
    Status = ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ExIoCompletionReserveObjectType);
    return NT_SUCCESS(Status);
}

NTSTATUS
NTAPI
NtAllocateReserveObject(PHANDLE ReserveHandle,
                        POBJECT_ATTRIBUTES ObjectAttributes,
                        MEMORY_RESERVE_OBJECT_TYPE Type)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_TYPE ObjectType;
    PEX_RESERVE_OBJECT Reserve;
    OBJECT_ATTRIBUTES CapturedAttributes;
    UNICODE_STRING CapturedName;
    HANDLE Handle;
    BOOLEAN Named = FALSE;
    NTSTATUS Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ReserveHandle);
            if (ObjectAttributes)
            {
                ProbeForRead(ObjectAttributes, sizeof(*ObjectAttributes), TYPE_ALIGNMENT(OBJECT_ATTRIBUTES));
                CapturedAttributes = *ObjectAttributes;
                if (CapturedAttributes.ObjectName)
                {
                    ProbeForRead(CapturedAttributes.ObjectName, sizeof(CapturedName), TYPE_ALIGNMENT(UNICODE_STRING));
                    CapturedName = *CapturedAttributes.ObjectName;
                    Named = CapturedName.Length != 0;
                }
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (ObjectAttributes && ObjectAttributes->ObjectName)
    {
        Named = ObjectAttributes->ObjectName->Length != 0;
    }

    if (Named)
        return STATUS_OBJECT_NAME_INVALID;
    if (Type == MemoryReserveObjectTypeUserApc)
        ObjectType = ExUserApcReserveObjectType;
    else if (Type == MemoryReserveObjectTypeIoCompletion)
        ObjectType = ExIoCompletionReserveObjectType;
    else
        return STATUS_INVALID_PARAMETER;

    Status = ObCreateObject(PreviousMode, ObjectType, ObjectAttributes, PreviousMode, NULL, sizeof(*Reserve), 0, 0, (PVOID *)&Reserve);
    if (!NT_SUCCESS(Status))
        return Status;

    Reserve->InUse = 0;
    Status = ObInsertObject(Reserve, NULL, GENERIC_READ | GENERIC_WRITE, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *ReserveHandle = Handle;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
        ObCloseHandle(Handle, PreviousMode);
    }
    _SEH2_END;

    return Status;
}
