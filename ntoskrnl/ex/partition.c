/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Executive memory-partition object services
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* PRIVATE DEFINITIONS *******************************************************/

#define MEMORY_PARTITION_QUERY_ACCESS  0x0001
#define MEMORY_PARTITION_MODIFY_ACCESS 0x0002
#define MEMORY_PARTITION_ALL_ACCESS                                      \
    (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE |                            \
     MEMORY_PARTITION_QUERY_ACCESS | MEMORY_PARTITION_MODIFY_ACCESS)

#define EXP_MEMORY_PARTITION_SIGNATURE 'traP'

typedef struct _EXP_MEMORY_PARTITION
{
    ULONG Signature;
    PEXP_MANAGE_MEMORY_PARTITION ManagePartition;
    PVOID ProviderContext;
} EXP_MEMORY_PARTITION, *PEXP_MEMORY_PARTITION;

/* GLOBALS *******************************************************************/

POBJECT_TYPE PsPartitionType;

static GENERIC_MAPPING ExpMemoryPartitionMapping =
{
    STANDARD_RIGHTS_READ | MEMORY_PARTITION_QUERY_ACCESS,
    STANDARD_RIGHTS_WRITE | MEMORY_PARTITION_MODIFY_ACCESS,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    MEMORY_PARTITION_ALL_ACCESS
};

/* INITIALIZATION ************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializePartitionImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    UNICODE_STRING TypeName;
    NTSTATUS Status;

    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    RtlInitUnicodeString(&TypeName, L"Partition");
    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);
    ObjectTypeInitializer.DefaultNonPagedPoolCharge =
        sizeof(EXP_MEMORY_PARTITION);
    ObjectTypeInitializer.GenericMapping = ExpMemoryPartitionMapping;
    ObjectTypeInitializer.PoolType = NonPagedPoolNx;
    ObjectTypeInitializer.ValidAccessMask = MEMORY_PARTITION_ALL_ACCESS;
    ObjectTypeInitializer.InvalidAttributes = OBJ_OPENLINK;

    Status = ObCreateObjectType(&TypeName,
                                &ObjectTypeInitializer,
                                NULL,
                                &PsPartitionType);
    return NT_SUCCESS(Status);
}

/* INTERNAL FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
ExpCreateMemoryPartition(
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PEXP_MANAGE_MEMORY_PARTITION ManagePartition,
    _In_opt_ PVOID ProviderContext,
    _Out_ PHANDLE PartitionHandle)
{
    PEXP_MEMORY_PARTITION Partition;
    NTSTATUS Status;

    PAGED_CODE();

    if (ManagePartition == NULL)
        return STATUS_INVALID_PARAMETER_3;
    if (PartitionHandle == NULL)
        return STATUS_INVALID_PARAMETER_5;

    Status = ObCreateObject(KernelMode,
                            PsPartitionType,
                            ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(*Partition),
                            0,
                            0,
                            (PVOID *)&Partition);
    if (!NT_SUCCESS(Status))
        return Status;

    Partition->Signature = EXP_MEMORY_PARTITION_SIGNATURE;
    Partition->ManagePartition = ManagePartition;
    Partition->ProviderContext = ProviderContext;

    return ObInsertObject(Partition,
                          NULL,
                          DesiredAccess,
                          0,
                          NULL,
                          PartitionHandle);
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
ZwOpenPartition(
    _Out_ PHANDLE PartitionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    PAGED_CODE();

    return ObOpenObjectByName(ObjectAttributes,
                              PsPartitionType,
                              KernelMode,
                              NULL,
                              DesiredAccess,
                              NULL,
                              PartitionHandle);
}

NTSTATUS
NTAPI
ZwManagePartition(
    _In_ HANDLE TargetHandle,
    _In_opt_ HANDLE SourceHandle,
    _In_ ULONG PartitionInformationClass,
    _Inout_updates_bytes_opt_(PartitionInformationLength) PVOID PartitionInformation,
    _In_ ULONG PartitionInformationLength)
{
    PEXP_MEMORY_PARTITION Partition;
    ACCESS_MASK DesiredAccess;
    NTSTATUS Status;

    PAGED_CODE();

    DesiredAccess = PartitionInformationClass == 0 ?
                        MEMORY_PARTITION_QUERY_ACCESS :
                        MEMORY_PARTITION_MODIFY_ACCESS;

    Status = ObReferenceObjectByHandle(TargetHandle,
                                       DesiredAccess,
                                       PsPartitionType,
                                       KernelMode,
                                       (PVOID *)&Partition,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    ASSERT(Partition->Signature == EXP_MEMORY_PARTITION_SIGNATURE);
    Status = Partition->ManagePartition(Partition->ProviderContext,
                                        SourceHandle,
                                        PartitionInformationClass,
                                        PartitionInformation,
                                        PartitionInformationLength);
    ObDereferenceObject(Partition);
    return Status;
}
