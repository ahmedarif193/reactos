/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel
 * FILE:            ntoskrnl/ex/profile.c
 * PURPOSE:         Support for Executive Profile Objects
 * PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
 *                  Thomas Weidenmueller
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

POBJECT_TYPE ExProfileObjectType = NULL;
KMUTEX ExpProfileMutex;

GENERIC_MAPPING ExpProfileMapping =
{
    STANDARD_RIGHTS_READ    | PROFILE_CONTROL,
    STANDARD_RIGHTS_WRITE   | PROFILE_CONTROL,
    STANDARD_RIGHTS_EXECUTE | PROFILE_CONTROL,
    PROFILE_ALL_ACCESS
};

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
ExpDeleteProfile(PVOID ObjectBody)
{
    PEPROFILE Profile;
    ULONG State;

    /* Typecast the Object */
    Profile = ObjectBody;

    /* Check if there if the Profile was started */
    if (Profile->LockedBufferAddress)
    {
        /* Stop the Profile */
        State = KeStopProfile(Profile->ProfileObject);
        ASSERT(State != FALSE);

        /* Unmap the Locked Buffer */
        MmUnmapLockedPages(Profile->LockedBufferAddress, Profile->Mdl);
        MmUnlockPages(Profile->Mdl);
        IoFreeMdl(Profile->Mdl);
        ExFreePoolWithTag(Profile->ProfileObject, TAG_PROFILE);
    }

    /* Check if a Process is associated and reference it */
    if (Profile->Process) ObDereferenceObject(Profile->Process);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeProfileImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    UNICODE_STRING Name;
    NTSTATUS Status;
    DPRINT("Creating Profile Object Type\n");

    /* Initialize the Mutex to lock the States */
    KeInitializeMutex(&ExpProfileMutex, 64);

    /* Create the Event Pair Object Type */
    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    RtlInitUnicodeString(&Name, L"Profile");
    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(KPROFILE);
    ObjectTypeInitializer.GenericMapping = ExpProfileMapping;
    ObjectTypeInitializer.PoolType = NonPagedPool;
    ObjectTypeInitializer.DeleteProcedure = ExpDeleteProfile;
    ObjectTypeInitializer.ValidAccessMask = PROFILE_ALL_ACCESS;
    ObjectTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
    Status = ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ExProfileObjectType);
    if (!NT_SUCCESS(Status)) return FALSE;
    return TRUE;
}

NTSTATUS
NTAPI
NtCreateProfile(OUT PHANDLE ProfileHandle,
                IN HANDLE Process OPTIONAL,
                IN PVOID RangeBase,
                IN SIZE_T RangeSize,
                IN ULONG BucketSize,
                IN PVOID Buffer,
                IN ULONG BufferSize,
                IN KPROFILE_SOURCE ProfileSource,
                IN KAFFINITY Affinity)
{
    HANDLE hProfile;
    PEPROFILE Profile;
    PEPROCESS pProcess;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    ULONG Log2 = 0;
    ULONG_PTR Segment = 0;
    SIZE_T BucketsRequired;
    PAGED_CODE();

    /* Easy way out */
    if (!BufferSize) return STATUS_INVALID_PARAMETER_7;
    if (!RangeSize) return STATUS_INVALID_PARAMETER_4;
    if (!(Affinity & KeActiveProcessors)) return STATUS_INVALID_PARAMETER_9;

    /* Check if this is a low-memory profile */
    if ((!BucketSize) && (RangeBase < (PVOID)(0x10000)))
    {
        /* Validate size */
        if (BufferSize < sizeof(ULONG)) return STATUS_INVALID_PARAMETER_7;

        /* This will become a segmented profile object */
        Segment = (ULONG_PTR)RangeBase;
        RangeBase = 0;

        /* Recalculate the bucket size */
        BucketSize = RangeSize / (BufferSize / sizeof(ULONG));

        /* Convert it to log2 */
        BucketSize--;
        while (BucketSize >>= 1) Log2++;
        BucketSize += Log2 + 1;
    }

    /* Validate bucket size */
    if ((BucketSize > 31) || (BucketSize < 2))
    {
        DPRINT1("Bucket size invalid\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Make sure that the buckets can map the range */
    BucketsRequired = RangeSize >> BucketSize;
    if (RangeSize & (((SIZE_T)1 << BucketSize) - 1))
    {
        BucketsRequired++;
    }
    if (BucketsRequired > BufferSize / sizeof(ULONG))
    {
        DPRINT1("Bucket size too small\n");
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Make sure that the range isn't too gigantic */
    if (RangeSize > MAXULONG_PTR - (ULONG_PTR)RangeBase)
    {
        DPRINT1("Range too big\n");
        return STATUS_BUFFER_OVERFLOW;
    }

    /* Check if we were called from user-mode */
    if(PreviousMode != KernelMode)
    {
        /* Entry SEH */
        _SEH2_TRY
        {
            /* Make sure that the handle pointer is valid */
            ProbeForWriteHandle(ProfileHandle);

            /* Check if the buffer is valid */
            ProbeForWrite(Buffer,
                          BufferSize,
                          sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Check if a process was specified */
    if (Process)
    {
        /* Reference it */
        Status = ObReferenceObjectByHandle(Process,
                                           PROCESS_QUERY_INFORMATION,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID*)&pProcess,
                                           NULL);
        if (!NT_SUCCESS(Status)) return(Status);
    }
    else
    {
        /* Segmented profile objects cannot be used system-wide */
        if (Segment) return STATUS_INVALID_PARAMETER;

        /* No process was specified, which means a System-Wide Profile */
        pProcess = NULL;

        /* For this, we need to check the Privilege */
        if(!SeSinglePrivilegeCheck(SeSystemProfilePrivilege, PreviousMode))
        {
            DPRINT1("NtCreateProfile: Caller requires the SeSystemProfilePrivilege privilege!\n");
            return STATUS_PRIVILEGE_NOT_HELD;
        }
    }

    /* Create the object */
    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               0,
                               NULL,
                               NULL);
    Status = ObCreateObject(KernelMode,
                            ExProfileObjectType,
                            &ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(EPROFILE),
                            0,
                            sizeof(EPROFILE) + sizeof(KPROFILE),
                            (PVOID*)&Profile);
    if (!NT_SUCCESS(Status))
    {
        /* Dereference the process object if it was specified */
        if (pProcess) ObDereferenceObject(pProcess);

        /* Return Status */
        return Status;
    }

    /* Initialize it */
    Profile->RangeBase = RangeBase;
    Profile->RangeSize = RangeSize;
    Profile->Buffer = Buffer;
    Profile->BufferSize = BufferSize;
    Profile->BucketSize = BucketSize;
    Profile->ProfileObject = NULL;
    Profile->LockedBufferAddress = NULL;
    Profile->Mdl = NULL;
    Profile->Segment = Segment;
    Profile->ProfileSource = ProfileSource;
    Profile->Affinity = Affinity;
    Profile->Process = pProcess;

    /* Insert into the Object Tree */
    Status = ObInsertObject ((PVOID)Profile,
                             NULL,
                             PROFILE_CONTROL,
                             0,
                             NULL,
                             &hProfile);

    /* Check for Success */
    if (!NT_SUCCESS(Status))
    {
        /* ObInsertObject deleted Profile; ExpDeleteProfile released Process. */
        return Status;
    }

    /* Enter SEH */
    _SEH2_TRY
    {
        /* Copy the created handle back to the caller*/
        *ProfileHandle = hProfile;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* Return Status */
    return Status;
}

NTSTATUS
NTAPI
NtQueryPerformanceCounter(OUT PLARGE_INTEGER PerformanceCounter,
                          OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER PerfFrequency;
    NTSTATUS Status = STATUS_SUCCESS;

    /* Check if we were called from user-mode */
    if (PreviousMode != KernelMode)
    {
        /* Entry SEH Block */
        _SEH2_TRY
        {
            /* Make sure the counter and frequency are valid */
            ProbeForWriteLargeInteger(PerformanceCounter);
            if (PerformanceFrequency)
            {
                ProbeForWriteLargeInteger(PerformanceFrequency);
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Enter a new SEH Block */
    _SEH2_TRY
    {
        /* Query the Kernel */
        *PerformanceCounter = KeQueryPerformanceCounter(&PerfFrequency);

        /* Return Frequency if requested */
        if (PerformanceFrequency) *PerformanceFrequency = PerfFrequency;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        /* Get the exception code */
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* Return status to caller */
    return Status;
}

NTSTATUS
NTAPI
NtStartProfile(IN HANDLE ProfileHandle)
{
    PEPROFILE Profile;
    PKPROFILE ProfileObject;
    PMDL Mdl;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID TempLockedBufferAddress;
    NTSTATUS Status;
    PAGED_CODE();

    /* Get the Object */
    Status = ObReferenceObjectByHandle(ProfileHandle,
                                       PROFILE_CONTROL,
                                       ExProfileObjectType,
                                       PreviousMode,
                                       (PVOID*)&Profile,
                                       NULL);
    if (!NT_SUCCESS(Status)) return(Status);

    /* To avoid a Race, wait on the Mutex */
    KeWaitForSingleObject(&ExpProfileMutex,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    /* The Profile can still be enabled though, so handle that */
    if (Profile->LockedBufferAddress)
    {
        /* Release our lock, dereference and return */
        KeReleaseMutex(&ExpProfileMutex, FALSE);
        ObDereferenceObject(Profile);
        return STATUS_PROFILING_NOT_STOPPED;
    }

    /* Allocate a Kernel Profile Object. */
    ProfileObject = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(*ProfileObject),
                                          TAG_PROFILE);
    if (!ProfileObject)
    {
        /* Out of memory, fail */
        KeReleaseMutex(&ExpProfileMutex, FALSE);
        ObDereferenceObject(Profile);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Reject a source for which no interrupt provider exists. */
    if (!KeQueryIntervalProfile(Profile->ProfileSource))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto FailureProfileObject;
    }

    /* Allocate the MDL without publishing partially initialized state. */
    Mdl = IoAllocateMdl(Profile->Buffer, Profile->BufferSize, FALSE, FALSE, NULL);
    if (!Mdl)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto FailureProfileObject;
    }

    /* Protect this in SEH as we might raise an exception */
    _SEH2_TRY
    {
        /* Probe and Lock for Write Access */
        MmProbeAndLockPages(Mdl, PreviousMode, IoWriteAccess);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
        IoFreeMdl(Mdl);
        _SEH2_YIELD(goto FailureProfileObject);
    }
    _SEH2_END;

    /* Map the pages */
    TempLockedBufferAddress = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (!TempLockedBufferAddress)
    {
        MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto FailureProfileObject;
    }

    /* Initialize the Kernel Profile Object */
    KeInitializeProfile(ProfileObject, Profile->Process ? &Profile->Process->Pcb : NULL, Profile->RangeBase, Profile->RangeSize, Profile->BucketSize, Profile->ProfileSource, Profile->Affinity);
    ProfileObject->Segment = Profile->Segment;

    /* Start the Profiling */
    if (!KeStartProfile(ProfileObject, TempLockedBufferAddress))
    {
        MmUnmapLockedPages(TempLockedBufferAddress, Mdl);
        MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
        Status = STATUS_PROFILING_NOT_STARTED;
        goto FailureProfileObject;
    }

    /* Publish the state only after every fallible operation succeeded. */
    Profile->ProfileObject = ProfileObject;
    Profile->Mdl = Mdl;
    Profile->LockedBufferAddress = TempLockedBufferAddress;

    /* Release mutex, dereference and return */
    KeReleaseMutex(&ExpProfileMutex, FALSE);
    ObDereferenceObject(Profile);
    return STATUS_SUCCESS;

FailureProfileObject:
    ExFreePoolWithTag(ProfileObject, TAG_PROFILE);
    KeReleaseMutex(&ExpProfileMutex, FALSE);
    ObDereferenceObject(Profile);
    return Status;
}

NTSTATUS
NTAPI
NtStopProfile(IN HANDLE ProfileHandle)
{
    PEPROFILE Profile;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    PAGED_CODE();

    /* Get the Object */
    Status = ObReferenceObjectByHandle(ProfileHandle,
                                       PROFILE_CONTROL,
                                       ExProfileObjectType,
                                       PreviousMode,
                                       (PVOID*)&Profile,
                                       NULL);
    if (!NT_SUCCESS(Status)) return(Status);

    /* Get the Mutex */
    KeWaitForSingleObject(&ExpProfileMutex,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    /* Make sure the Profile Object is really Started */
    if (!Profile->LockedBufferAddress)
    {
        Status = STATUS_PROFILING_NOT_STARTED;
        goto Exit;
    }

    /* Stop the Profile */
    if (!KeStopProfile(Profile->ProfileObject))
    {
        Status = STATUS_PROFILING_NOT_STARTED;
        goto Exit;
    }

    /* Unlock the Buffer */
    MmUnmapLockedPages(Profile->LockedBufferAddress, Profile->Mdl);
    MmUnlockPages(Profile->Mdl);
    IoFreeMdl(Profile->Mdl);
    ExFreePoolWithTag(Profile->ProfileObject, TAG_PROFILE);

    /* Clear the Locked Buffer pointer, meaning the Object is Stopped */
    Profile->LockedBufferAddress = NULL;
    Profile->Mdl = NULL;
    Profile->ProfileObject = NULL;

Exit:
    /* Release Mutex, Dereference and Return */
    KeReleaseMutex(&ExpProfileMutex, FALSE);
    ObDereferenceObject(Profile);
    return Status;
}

NTSTATUS
NTAPI
NtQueryIntervalProfile(IN KPROFILE_SOURCE ProfileSource,
                       OUT PULONG Interval)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG ReturnInterval;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    /* Check if we were called from user-mode */
    if (PreviousMode != KernelMode)
    {
        /* Enter SEH Block */
        _SEH2_TRY
        {
            /* Validate interval */
            ProbeForWriteUlong(Interval);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Query the Interval */
    ReturnInterval = (ULONG)KeQueryIntervalProfile(ProfileSource);

    /* Enter SEH block for return */
    _SEH2_TRY
    {
        /* Return the data */
        *Interval = ReturnInterval;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        /* Get the exception code */
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* Return Success */
    return Status;
}

NTSTATUS
NTAPI
NtSetIntervalProfile(IN ULONG Interval,
                     IN KPROFILE_SOURCE Source)
{
    /* Let the Kernel do the job */
    KeSetIntervalProfile(Interval, Source);

    /* Nothing can go wrong */
    return STATUS_SUCCESS;
}
