/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Win8.1 WDM/DDK export ABI coverage
 */

#include <kmt_test.h>

#define WDM_NT81_TARGET_NTDDI 0x06030000
#define WDM_NT81_TARGET_WIN32_WINNT 0x0603
#define WDM_NT81_EXPECTED_EXPORTS 435
#define WDM_NT81_TRACKED_GAPS 180
#define WDM_NT81_TOTAL_EXPORTS (WDM_NT81_EXPECTED_EXPORTS + WDM_NT81_TRACKED_GAPS)
#define REACTOS_SHARED_MAGIC 0x8eac705

static const PCSTR RequiredWdmNt81Exports[] =
{
    "DbgBreakPointWithStatus",
    "DbgPrintEx",
    "DbgPrintReturnControlC",
    "DbgQueryDebugFilterState",
    "DbgSetDebugFilterState",
    "ExAcquireFastMutex",
    "ExAcquireFastMutexUnsafe",
    "ExAcquireResourceExclusiveLite",
    "ExAcquireResourceSharedLite",
    "ExAcquireRundownProtection",
    "ExAcquireRundownProtectionCacheAware",
    "ExAcquireRundownProtectionCacheAwareEx",
    "ExAcquireRundownProtectionEx",
    "ExAcquireSharedStarveExclusive",
    "ExAcquireSharedWaitForExclusive",
    "ExAllocateCacheAwareRundownProtection",
    "ExAllocatePool",
    "ExAllocatePoolWithQuota",
    "ExAllocatePoolWithQuotaTag",
    "ExAllocatePoolWithTag",
    "ExAllocatePoolWithTagPriority",
    "ExConvertExclusiveToSharedLite",
    "ExCreateCallback",
    "ExDeleteNPagedLookasideList",
    "ExDeletePagedLookasideList",
    "ExDeleteResourceLite",
    "ExEnterCriticalRegionAndAcquireResourceExclusive",
    "ExEnterCriticalRegionAndAcquireResourceShared",
    "ExEnterCriticalRegionAndAcquireSharedWaitForExclusive",
    "ExFreeCacheAwareRundownProtection",
    "ExFreePool",
    "ExFreePoolWithTag",
    "ExGetExclusiveWaiterCount",
    "ExGetPreviousMode",
    "ExGetSharedWaiterCount",
    "ExInitializeNPagedLookasideList",
    "ExInitializePagedLookasideList",
    "ExInitializeResourceLite",
    "ExInitializeRundownProtection",
    "ExInitializeRundownProtectionCacheAware",
    "ExInterlockedAddLargeInteger",
    "ExInterlockedAddUlong",
    "ExInterlockedInsertHeadList",
    "ExInterlockedInsertTailList",
    "ExInterlockedPopEntryList",
    "ExInterlockedPushEntryList",
    "ExInterlockedRemoveHeadList",
    "ExIsProcessorFeaturePresent",
    "ExIsResourceAcquiredExclusiveLite",
    "ExIsResourceAcquiredSharedLite",
    "ExLocalTimeToSystemTime",
    "ExNotifyCallback",
    "ExQueryDepthSList",
    "ExQueueWorkItem",
    "ExRaiseStatus",
    "ExReInitializeRundownProtection",
    "ExReInitializeRundownProtectionCacheAware",
    "ExRegisterCallback",
    "ExReinitializeResourceLite",
    "ExReleaseFastMutex",
    "ExReleaseFastMutexUnsafe",
    "ExReleaseResourceAndLeaveCriticalRegion",
    "ExReleaseResourceForThreadLite",
    "ExReleaseResourceLite",
    "ExReleaseRundownProtection",
    "ExReleaseRundownProtectionCacheAware",
    "ExReleaseRundownProtectionCacheAwareEx",
    "ExReleaseRundownProtectionEx",
    "ExRundownCompleted",
    "ExRundownCompletedCacheAware",
    "ExSetResourceOwnerPointer",
    "ExSetTimerResolution",
    "ExSizeOfRundownProtectionCacheAware",
    "ExSystemTimeToLocalTime",
    "ExTryToAcquireFastMutex",
    "ExUnregisterCallback",
    "ExVerifySuite",
    "ExWaitForRundownProtectionRelease",
    "ExWaitForRundownProtectionReleaseCacheAware",
    "ExpInterlockedFlushSList",
    "ExpInterlockedPopEntrySList",
    "ExpInterlockedPushEntrySList",
    "IoAcquireCancelSpinLock",
    "IoAcquireRemoveLockEx",
    "IoAllocateDriverObjectExtension",
    "IoAllocateErrorLogEntry",
    "IoAllocateIrp",
    "IoAllocateMdl",
    "IoAllocateWorkItem",
    "IoAttachDevice",
    "IoAttachDeviceToDeviceStack",
    "IoBuildAsynchronousFsdRequest",
    "IoBuildDeviceIoControlRequest",
    "IoBuildPartialMdl",
    "IoBuildSynchronousFsdRequest",
    "IoCancelIrp",
    "IoCheckShareAccess",
    "IoConnectInterrupt",
    "IoConnectInterruptEx",
    "IoCreateDevice",
    "IoCreateFile",
    "IoCreateNotificationEvent",
    "IoCreateSymbolicLink",
    "IoCreateSynchronizationEvent",
    "IoCreateUnprotectedSymbolicLink",
    "IoCsqInitialize",
    "IoCsqInitializeEx",
    "IoCsqInsertIrp",
    "IoCsqInsertIrpEx",
    "IoCsqRemoveIrp",
    "IoCsqRemoveNextIrp",
    "IoDeleteDevice",
    "IoDeleteSymbolicLink",
    "IoDetachDevice",
    "IoDisconnectInterrupt",
    "IoDisconnectInterruptEx",
    "IoForwardIrpSynchronously",
    "IoFreeErrorLogEntry",
    "IoFreeIrp",
    "IoFreeMdl",
    "IoFreeWorkItem",
    "IoGetAttachedDeviceReference",
    "IoGetBootDiskInformation",
    "IoGetCurrentProcess",
    "IoGetDeviceInterfaceAlias",
    "IoGetDeviceInterfaces",
    "IoGetDeviceObjectPointer",
    "IoGetDeviceProperty",
    "IoGetDmaAdapter",
    "IoGetDriverObjectExtension",
    "IoGetInitialStack",
    "IoGetRelatedDeviceObject",
    "IoGetStackLimits",
    "IoGetTopLevelIrp",
    "IoInitializeIrp",
    "IoInitializeRemoveLockEx",
    "IoInitializeTimer",
    "IoInvalidateDeviceRelations",
    "IoInvalidateDeviceState",
    "IoIs32bitProcess",
    "IoIsWdmVersionAvailable",
    "IoOpenDeviceInterfaceRegistryKey",
    "IoOpenDeviceRegistryKey",
    "IoQueueWorkItem",
    "IoRegisterDeviceInterface",
    "IoRegisterLastChanceShutdownNotification",
    "IoRegisterPlugPlayNotification",
    "IoRegisterShutdownNotification",
    "IoReleaseCancelSpinLock",
    "IoReleaseRemoveLockAndWaitEx",
    "IoReleaseRemoveLockEx",
    "IoRemoveShareAccess",
    "IoReportTargetDeviceChange",
    "IoReportTargetDeviceChangeAsynchronous",
    "IoRequestDeviceEject",
    "IoReuseIrp",
    "IoSetCompletionRoutineEx",
    "IoSetDeviceInterfaceState",
    "IoSetShareAccess",
    "IoSetTopLevelIrp",
    "IoStartNextPacket",
    "IoStartNextPacketByKey",
    "IoStartPacket",
    "IoStartTimer",
    "IoStopTimer",
    "IoUnregisterPlugPlayNotification",
    "IoUnregisterShutdownNotification",
    "IoUpdateShareAccess",
    "IoValidateDeviceIoControlAccess",
    "IoWMIAllocateInstanceIds",
    "IoWMIDeviceObjectToInstanceName",
    "IoWMIDeviceObjectToProviderId",
    "IoWMIExecuteMethod",
    "IoWMIHandleToInstanceName",
    "IoWMIOpenBlock",
    "IoWMIQueryAllData",
    "IoWMIQueryAllDataMultiple",
    "IoWMIQuerySingleInstance",
    "IoWMIQuerySingleInstanceMultiple",
    "IoWMIRegistrationControl",
    "IoWMISetNotificationCallback",
    "IoWMISetSingleInstance",
    "IoWMISetSingleItem",
    "IoWMISuggestInstanceName",
    "IoWMIWriteEvent",
    "IoWriteErrorLogEntry",
    "IofCallDriver",
    "IofCompleteRequest",
    "KdDisableDebugger",
    "KdEnableDebugger",
    "KdRefreshDebuggerNotPresent",
    "KeAcquireGuardedMutex",
    "KeAcquireGuardedMutexUnsafe",
    "KeAcquireInStackQueuedSpinLock",
    "KeAcquireInStackQueuedSpinLockAtDpcLevel",
    "KeAcquireInStackQueuedSpinLockForDpc",
    "KeAcquireInterruptSpinLock",
    "KeAcquireSpinLockAtDpcLevel",
    "KeAcquireSpinLockForDpc",
    "KeAcquireSpinLockRaiseToDpc",
    "KeAreAllApcsDisabled",
    "KeAreApcsDisabled",
    "KeBugCheckEx",
    "KeCancelTimer",
    "KeClearEvent",
    "KeDelayExecutionThread",
    "KeDeregisterBugCheckCallback",
    "KeDeregisterBugCheckReasonCallback",
    "KeDeregisterNmiCallback",
    "KeEnterCriticalRegion",
    "KeEnterGuardedRegion",
    "KeFlushQueuedDpcs",
    "KeFlushWriteBuffer",
    "KeGetRecommendedSharedDataAlignment",
    "KeInitializeCrashDumpHeader",
    "KeInitializeDeviceQueue",
    "KeInitializeDpc",
    "KeInitializeEvent",
    "KeInitializeGuardedMutex",
    "KeInitializeMutex",
    "KeInitializeSemaphore",
    "KeInitializeSpinLock",
    "KeInitializeThreadedDpc",
    "KeInitializeTimer",
    "KeInitializeTimerEx",
    "KeInsertByKeyDeviceQueue",
    "KeInsertDeviceQueue",
    "KeInsertQueueDpc",
    "KeIpiGenericCall",
    "KeLeaveCriticalRegion",
    "KeLeaveGuardedRegion",
    "KeLowerIrql",
    "KeQueryActiveProcessors",
    "KeQueryPerformanceCounter",
    "KeQueryPriorityThread",
    "KeQueryRuntimeThread",
    "KeQueryTimeIncrement",
    "KeReadStateEvent",
    "KeReadStateMutex",
    "KeReadStateSemaphore",
    "KeReadStateTimer",
    "KeRegisterBugCheckCallback",
    "KeRegisterBugCheckReasonCallback",
    "KeRegisterNmiCallback",
    "KeReleaseGuardedMutex",
    "KeReleaseGuardedMutexUnsafe",
    "KeReleaseInStackQueuedSpinLock",
    "KeReleaseInStackQueuedSpinLockForDpc",
    "KeReleaseInStackQueuedSpinLockFromDpcLevel",
    "KeReleaseInterruptSpinLock",
    "KeReleaseMutex",
    "KeReleaseSemaphore",
    "KeReleaseSpinLock",
    "KeReleaseSpinLockForDpc",
    "KeReleaseSpinLockFromDpcLevel",
    "KeRemoveByKeyDeviceQueue",
    "KeRemoveByKeyDeviceQueueIfBusy",
    "KeRemoveDeviceQueue",
    "KeRemoveEntryDeviceQueue",
    "KeRemoveQueueDpc",
    "KeResetEvent",
    "KeRevertToUserAffinityThread",
    "KeSetEvent",
    "KeSetImportanceDpc",
    "KeSetPriorityThread",
    "KeSetSystemAffinityThread",
    "KeSetTargetProcessorDpc",
    "KeSetTimer",
    "KeSetTimerEx",
    "KeStallExecutionProcessor",
    "KeSynchronizeExecution",
    "KeTestSpinLock",
    "KeTryToAcquireGuardedMutex",
    "KeTryToAcquireSpinLockAtDpcLevel",
    "KeWaitForMultipleObjects",
    "KeWaitForSingleObject",
    "MmAddVerifierThunks",
    "MmAdvanceMdl",
    "MmAllocateContiguousMemory",
    "MmAllocateContiguousMemorySpecifyCache",
    "MmAllocateMappingAddress",
    "MmAllocatePagesForMdl",
    "MmAllocatePagesForMdlEx",
    "MmBuildMdlForNonPagedPool",
    "MmCreateMdl",
    "MmFreeContiguousMemory",
    "MmFreeContiguousMemorySpecifyCache",
    "MmFreeMappingAddress",
    "MmFreePagesFromMdl",
    "MmGetSystemRoutineAddress",
    "MmIsDriverVerifying",
    "MmIsIoSpaceActive",
    "MmIsVerifierEnabled",
    "MmLockPagableDataSection",
    "MmMapIoSpace",
    "MmMapLockedPages",
    "MmMapLockedPagesSpecifyCache",
    "MmMapLockedPagesWithReservedMapping",
    "MmPageEntireDriver",
    "MmProbeAndLockPages",
    "MmProbeAndLockProcessPages",
    "MmProbeAndLockSelectedPages",
    "MmProtectMdlSystemAddress",
    "MmQuerySystemSize",
    "MmResetDriverPaging",
    "MmSizeOfMdl",
    "MmUnlockPagableImageSection",
    "MmUnlockPages",
    "MmUnmapIoSpace",
    "MmUnmapLockedPages",
    "MmUnmapReservedMapping",
    "ObCloseHandle",
    "ObGetObjectSecurity",
    "ObReferenceObjectByHandle",
    "ObReferenceObjectByPointer",
    "ObReleaseObjectSecurity",
    "ObfDereferenceObject",
    "ObfReferenceObject",
    "PoCallDriver",
    "PoRegisterDeviceForIdleDetection",
    "PoRegisterSystemState",
    "PoRequestPowerIrp",
    "PoSetHiberRange",
    "PoSetPowerState",
    "PoSetSystemState",
    "PoStartNextPowerIrp",
    "PoUnregisterSystemState",
    "PsCreateSystemThread",
    "PsGetVersion",
    "PsTerminateSystemThread",
    "PsWrapApcWow64Thread",
    "RtlAnsiStringToUnicodeString",
    "RtlAppendUnicodeStringToString",
    "RtlAppendUnicodeToString",
    "RtlAreBitsClear",
    "RtlAreBitsSet",
    "RtlAssert",
    "RtlCheckRegistryKey",
    "RtlClearAllBits",
    "RtlClearBit",
    "RtlClearBits",
    "RtlCompareMemory",
    "RtlCompareUnicodeString",
    "RtlCopyMemoryNonTemporal",
    "RtlCopyUnicodeString",
    "RtlCreateRegistryKey",
    "RtlCreateSecurityDescriptor",
    "RtlDeleteRegistryValue",
    "RtlEqualUnicodeString",
    "RtlFindClearBits",
    "RtlFindClearBitsAndSet",
    "RtlFindClearRuns",
    "RtlFindFirstRunClear",
    "RtlFindLastBackwardRunClear",
    "RtlFindLeastSignificantBit",
    "RtlFindLongestRunClear",
    "RtlFindMostSignificantBit",
    "RtlFindNextForwardRunClear",
    "RtlFindSetBits",
    "RtlFindSetBitsAndClear",
    "RtlFreeAnsiString",
    "RtlFreeUnicodeString",
    "RtlGUIDFromString",
    "RtlGetVersion",
    "RtlHashUnicodeString",
    "RtlInitAnsiString",
    "RtlInitAnsiStringEx",
    "RtlInitString",
    "RtlInitUnicodeString",
    "RtlInitializeBitMap",
    "RtlInt64ToUnicodeString",
    "RtlIntegerToUnicodeString",
    "RtlLengthSecurityDescriptor",
    "RtlNumberOfClearBits",
    "RtlNumberOfSetBits",
    "RtlPrefetchMemoryNonTemporal",
    "RtlQueryRegistryValues",
    "RtlSetAllBits",
    "RtlSetBit",
    "RtlSetBits",
    "RtlSetDaclSecurityDescriptor",
    "RtlStringFromGUID",
    "RtlTestBit",
    "RtlTimeFieldsToTime",
    "RtlTimeToTimeFields",
    "RtlUnicodeStringToAnsiString",
    "RtlUnicodeStringToInteger",
    "RtlUpcaseUnicodeChar",
    "RtlValidRelativeSecurityDescriptor",
    "RtlValidSecurityDescriptor",
    "RtlVerifyVersionInfo",
    "RtlWriteRegistryValue",
    "RtlxAnsiStringToUnicodeSize",
    "RtlxUnicodeStringToAnsiSize",
    "SeAccessCheck",
    "SeAssignSecurity",
    "SeAssignSecurityEx",
    "SeCaptureSubjectContext",
    "SeDeassignSecurity",
    "SeLockSubjectContext",
    "SeReleaseSubjectContext",
    "SeUnlockSubjectContext",
    "SeValidSecurityDescriptor",
    "ZwClose",
    "ZwCreateDirectoryObject",
    "ZwCreateFile",
    "ZwCreateKey",
    "ZwCreateSection",
    "ZwDeleteKey",
    "ZwDeleteValueKey",
    "ZwEnumerateKey",
    "ZwEnumerateValueKey",
    "ZwFlushKey",
    "ZwLoadDriver",
    "ZwMakeTemporaryObject",
    "ZwMapViewOfSection",
    "ZwOpenEvent",
    "ZwOpenFile",
    "ZwOpenKey",
    "ZwOpenSection",
    "ZwOpenSymbolicLinkObject",
    "ZwQueryFullAttributesFile",
    "ZwQueryInformationFile",
    "ZwQueryKey",
    "ZwQuerySymbolicLinkObject",
    "ZwQueryValueKey",
    "ZwReadFile",
    "ZwRestoreKey",
    "ZwSaveKey",
    "ZwSaveKeyEx",
    "ZwSetInformationFile",
    "ZwSetValueKey",
    "ZwUnloadDriver",
    "ZwUnmapViewOfSection",
    "ZwWriteFile",
};

static const PCSTR KnownWdmNt81ExportGaps[] =
{
    "DbgSetDebugPrintCallback",
    "ExAcquireSpinLockExclusive",
    "ExAcquireSpinLockExclusiveAtDpcLevel",
    "ExAcquireSpinLockShared",
    "ExAcquireSpinLockSharedAtDpcLevel",
    "ExAllocateTimer",
    "ExCancelTimer",
    "ExCleanupRundownProtectionCacheAware",
    "ExDeleteLookasideListEx",
    "ExDeleteTimer",
    "ExFlushLookasideListEx",
    "ExGetFirmwareEnvironmentVariable",
    "ExInitializeLookasideListEx",
    "ExInitializeRundownProtectionCacheAwareEx",
    "ExQueryTimerResolution",
    "ExReleaseSpinLockExclusive",
    "ExReleaseSpinLockExclusiveFromDpcLevel",
    "ExReleaseSpinLockShared",
    "ExReleaseSpinLockSharedFromDpcLevel",
    "ExSetFirmwareEnvironmentVariable",
    "ExSetResourceOwnerPointerEx",
    "ExSetTimer",
    "ExTryConvertSharedSpinLockExclusive",
    "IoCheckShareAccessEx",
    "IoGetAffinityInterrupt",
    "IoGetBootDiskInformationLite",
    "IoGetDeviceInterfacePropertyData",
    "IoGetDeviceNumaNode",
    "IoGetDevicePropertyData",
    "IoIsInitiator32bitProcess",
    "IoReplacePartitionUnit",
    "IoReportInterruptActive",
    "IoRequestDeviceEjectEx",
    "IoSetDeviceInterfacePropertyData",
    "IoSetDevicePropertyData",
    "IoSetShareAccessEx",
    "IoSynchronousCallDriver",
    "IoUnregisterPlugPlayNotificationEx",
    "KeGetCurrentNodeNumber",
    "KeGetCurrentProcessorNumberEx",
    "KeQueryActiveGroupCount",
    "KeQueryActiveProcessorCount",
    "KeQueryActiveProcessorCountEx",
    "KeQueryDpcWatchdogInformation",
    "KeQueryGroupAffinity",
    "KeQueryHighestNodeNumber",
    "KeQueryLogicalProcessorRelationship",
    "KeQueryMaximumGroupCount",
    "KeQueryMaximumProcessorCount",
    "KeQueryMaximumProcessorCountEx",
    "KeQueryNodeActiveAffinity",
    "KeQueryNodeMaximumProcessorCount",
    "KeQuerySystemTimePrecise",
    "KeQueryUnbiasedInterruptTime",
    "KeRemoveQueueDpcEx",
    "KeRestoreExtendedProcessorState",
    "KeRevertToUserAffinityThreadEx",
    "KeRevertToUserGroupAffinityThread",
    "KeSaveExtendedProcessorState",
    "KeSetCoalescableTimer",
    "KeSetSystemAffinityThreadEx",
    "KeSetSystemGroupAffinityThread",
    "KeSetTargetProcessorDpcEx",
    "KeShouldYieldProcessor",
    "MmAddVerifierSpecialThunks",
    "MmAllocateContiguousMemorySpecifyCacheNode",
    "MmAllocateContiguousNodeMemory",
    "MmAllocateMdlForIoSpace",
    "MmAllocateNodePagesForMdlEx",
    "MmAreMdlPagesCached",
    "MmIsDriverSuspectForVerifier",
    "MmIsDriverVerifyingByAddress",
    "MmMdlPageContentsState",
    "ObDereferenceObjectDeferDelete",
    "ObDereferenceObjectDeferDeleteWithTag",
    "ObGetFilterVersion",
    "ObReferenceObjectByHandleWithTag",
    "ObReferenceObjectByPointerWithTag",
    "ObReferenceObjectSafe",
    "ObReferenceObjectSafeWithTag",
    "ObRegisterCallbacks",
    "ObUnRegisterCallbacks",
    "ObfDereferenceObjectWithTag",
    "ObfReferenceObjectWithTag",
    "PoClearPowerRequest",
    "PoCreatePowerRequest",
    "PoDeletePowerRequest",
    "PoEndDeviceBusy",
    "PoFxActivateComponent",
    "PoFxCompleteDevicePowerNotRequired",
    "PoFxCompleteIdleCondition",
    "PoFxCompleteIdleState",
    "PoFxIdleComponent",
    "PoFxNotifySurprisePowerOn",
    "PoFxPowerControl",
    "PoFxPowerOnCrashdumpDevice",
    "PoFxRegisterCrashdumpDevice",
    "PoFxRegisterDevice",
    "PoFxReportDevicePoweredOn",
    "PoFxSetComponentLatency",
    "PoFxSetComponentResidency",
    "PoFxSetComponentWake",
    "PoFxSetDeviceIdleTimeout",
    "PoFxStartDevicePowerManagement",
    "PoFxUnregisterDevice",
    "PoGetSystemWake",
    "PoQueryWatchdogTime",
    "PoRegisterPowerSettingCallback",
    "PoSetDeviceBusyEx",
    "PoSetPowerRequest",
    "PoSetSystemWake",
    "PoStartDeviceBusy",
    "PoUnregisterPowerSettingCallback",
    "RtlCmDecodeMemIoResource",
    "RtlCmEncodeMemIoResource",
    "RtlCompareUnicodeStrings",
    "RtlCopyBitMap",
    "RtlCrc32",
    "RtlCrc64",
    "RtlDowncaseUnicodeChar",
    "RtlExtractBitMap",
    "RtlFindClosestEncodableLength",
    "RtlGenerateClass5Guid",
    "RtlIoDecodeMemIoResource",
    "RtlIoEncodeMemIoResource",
    "RtlIsUntrustedObject",
    "RtlNumberOfClearBitsInRange",
    "RtlNumberOfSetBitsInRange",
    "RtlNumberOfSetBitsUlongPtr",
    "RtlQueryValidationRunlevel",
    "RtlUTF8ToUnicodeN",
    "RtlUlongByteSwap",
    "RtlUlonglongByteSwap",
    "RtlUnicodeToUTF8N",
    "RtlUshortByteSwap",
    "SeComputeAutoInheritByObjectType",
    "SeEtwWriteKMCveEvent",
    "SeObjectCreateSaclAccessBits",
    "SeRegisterImageVerificationCallback",
    "SeUnregisterImageVerificationCallback",
    "ZwCommitComplete",
    "ZwCommitEnlistment",
    "ZwCommitTransaction",
    "ZwCreateEnlistment",
    "ZwCreateKeyTransacted",
    "ZwCreateResourceManager",
    "ZwCreateTransaction",
    "ZwCreateTransactionManager",
    "ZwEnumerateTransactionObject",
    "ZwGetNotificationResourceManager",
    "ZwOpenEnlistment",
    "ZwOpenKeyEx",
    "ZwOpenKeyTransacted",
    "ZwOpenKeyTransactedEx",
    "ZwOpenResourceManager",
    "ZwOpenTransaction",
    "ZwOpenTransactionManager",
    "ZwPrePrepareComplete",
    "ZwPrePrepareEnlistment",
    "ZwPrepareComplete",
    "ZwPrepareEnlistment",
    "ZwQueryInformationEnlistment",
    "ZwQueryInformationResourceManager",
    "ZwQueryInformationTransaction",
    "ZwQueryInformationTransactionManager",
    "ZwReadOnlyEnlistment",
    "ZwRecoverEnlistment",
    "ZwRecoverResourceManager",
    "ZwRecoverTransactionManager",
    "ZwRenameKey",
    "ZwRollbackComplete",
    "ZwRollbackEnlistment",
    "ZwRollbackTransaction",
    "ZwRollforwardTransactionManager",
    "ZwSetInformationEnlistment",
    "ZwSetInformationKey",
    "ZwSetInformationResourceManager",
    "ZwSetInformationTransaction",
    "ZwSetInformationTransactionManager",
    "ZwSinglePhaseReject",
};

static const PCSTR Win7Amd64InlineExports[] =
{
    "KeInitializeSpinLock",
};

static const PCSTR Win81AbsentTrackedExports[] =
{
    "ExCleanupRundownProtectionCacheAware",
    "ExInitializeRundownProtectionCacheAwareEx",
    "KeShouldYieldProcessor",
    "MmAddVerifierSpecialThunks",
    "RtlUlongByteSwap",
    "RtlUlonglongByteSwap",
    "RtlUshortByteSwap",
    "SeEtwWriteKMCveEvent",
    "SeObjectCreateSaclAccessBits",
    "ZwRollforwardTransactionManager",
    "ZwSetInformationTransactionManager",
    "ZwSinglePhaseReject",
};

static ULONG RuntimeNtVersion;
static BOOLEAN RuntimeIsReactOS;

static
PVOID
LookupRoutine(
    _In_z_ PCSTR RoutineName)
{
    WCHAR Buffer[128];
    UNICODE_STRING UnicodeName;
    SIZE_T Index;

    for (Index = 0; RoutineName[Index] != 0; ++Index)
    {
        if (Index >= RTL_NUMBER_OF(Buffer) - 1)
            return NULL;

        Buffer[Index] = (WCHAR)(UCHAR)RoutineName[Index];
    }

    Buffer[Index] = UNICODE_NULL;
    RtlInitUnicodeString(&UnicodeName, Buffer);
    return MmGetSystemRoutineAddress(&UnicodeName);
}

static
VOID
CaptureRuntimeVersion(VOID)
{
    NTSTATUS Status;
    RTL_OSVERSIONINFOEXW VersionInfo;

    RuntimeIsReactOS = *(PULONG)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == REACTOS_SHARED_MAGIC;

    RtlZeroMemory(&VersionInfo, sizeof(VersionInfo));
    VersionInfo.dwOSVersionInfoSize = sizeof(VersionInfo);
    Status = RtlGetVersion((PRTL_OSVERSIONINFOW)&VersionInfo);
    if (NT_SUCCESS(Status))
    {
        RuntimeNtVersion = (VersionInfo.dwMajorVersion << 8) | VersionInfo.dwMinorVersion;
        trace("Runtime NT version %lu.%lu build %lu, ReactOS=%u, WDM matrix target NTDDI 0x%08lx\n",
              VersionInfo.dwMajorVersion,
              VersionInfo.dwMinorVersion,
              VersionInfo.dwBuildNumber,
              RuntimeIsReactOS,
              WDM_NT81_TARGET_NTDDI);
    }
    else
    {
        RuntimeNtVersion = 0;
        trace("RtlGetVersion failed with 0x%08lx; continuing export ABI checks\n", Status);
    }
}

static
BOOLEAN
StringEquals(
    _In_z_ PCSTR Left,
    _In_z_ PCSTR Right)
{
    while (*Left != ANSI_NULL && *Right != ANSI_NULL)
    {
        if (*Left != *Right)
            return FALSE;

        ++Left;
        ++Right;
    }

    return *Left == *Right;
}

static
BOOLEAN
IsNameInList(
    _In_z_ PCSTR Name,
    _In_reads_(Count) const PCSTR *List,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; ++Index)
    {
        if (StringEquals(Name, List[Index]))
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
IsExpectedMissingRequiredExport(
    _In_z_ PCSTR Name)
{
#if defined(_M_AMD64)
    if (!RuntimeIsReactOS &&
        RuntimeNtVersion <= _WIN32_WINNT_WIN7 &&
        IsNameInList(Name, Win7Amd64InlineExports, RTL_NUMBER_OF(Win7Amd64InlineExports)))
    {
        return TRUE;
    }
#endif

    return FALSE;
}

static
VOID
CheckRequiredExports(VOID)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(RequiredWdmNt81Exports); ++Index)
    {
        PCSTR Name = RequiredWdmNt81Exports[Index];
        PVOID Routine = LookupRoutine(Name);

        if (IsExpectedMissingRequiredExport(Name))
        {
            ok(Routine == NULL, "%s unexpectedly resolves on this NT runtime\n", Name);
        }
        else
        {
            ok(Routine != NULL, "Win8.1 WDM export %s is missing\n", Name);
        }
    }
}

static
VOID
CheckTrackedGaps(VOID)
{
    ULONG Index;
    ULONG Resolved = 0;
    ULONG ExpectedResolved = 0;

    for (Index = 0; Index < RTL_NUMBER_OF(KnownWdmNt81ExportGaps); ++Index)
    {
        PCSTR Name = KnownWdmNt81ExportGaps[Index];
        PVOID Routine = LookupRoutine(Name);

        if (Routine != NULL)
            ++Resolved;

        if (RuntimeNtVersion >= _WIN32_WINNT_WINBLUE && !RuntimeIsReactOS)
        {
            if (RuntimeNtVersion == _WIN32_WINNT_WINBLUE &&
                IsNameInList(Name, Win81AbsentTrackedExports, RTL_NUMBER_OF(Win81AbsentTrackedExports)))
            {
                ok(Routine == NULL, "Win8.1-absent WDM export %s unexpectedly resolves on NT 6.3 runtime\n", Name);
            }
            else
            {
                ++ExpectedResolved;
                ok(Routine != NULL, "Win8.1 WDM export %s is missing on NT 6.3+ runtime\n", Name);
            }
        }
        else if (RuntimeIsReactOS)
        {
            ok(Routine == NULL, "Tracked ReactOS Win8.1 WDM gap %s is now resolvable; promote it to required exports\n", Name);
        }
        else
        {
            ok(TRUE, "Win8.1 WDM gap %s is version-gated on this runtime\n", Name);
        }
    }

    trace("Win8.1 WDM matrix: %u required exports checked, %u tracked gap expectations, %u gaps resolvable at runtime, %u expected resolvable\n",
          (ULONG)RTL_NUMBER_OF(RequiredWdmNt81Exports),
          (ULONG)RTL_NUMBER_OF(KnownWdmNt81ExportGaps),
          Resolved,
          ExpectedResolved);
    trace("Tracked gap groups: Ex timer/spinlock/lookaside, Io property/share/NUMA, Ke processor-group/time, Mm node/IO-space MDL, Ob callbacks/tags, PoFx/power requests, Rtl UTF8/bitmaps/byteswap, Se image verification, Zw KTM/transacted-key\n");
}

START_TEST(WdmNt81Abi)
{
    ok_eq_hex(WDM_NT81_TARGET_NTDDI, NTDDI_WINBLUE);
    ok_eq_hex(WDM_NT81_TARGET_WIN32_WINNT, _WIN32_WINNT_WINBLUE);
    ok_eq_ulong(RTL_NUMBER_OF(RequiredWdmNt81Exports), WDM_NT81_EXPECTED_EXPORTS);
    ok_eq_ulong(RTL_NUMBER_OF(KnownWdmNt81ExportGaps), WDM_NT81_TRACKED_GAPS);
    ok_eq_ulong(RTL_NUMBER_OF(RequiredWdmNt81Exports) + RTL_NUMBER_OF(KnownWdmNt81ExportGaps), WDM_NT81_TOTAL_EXPORTS);

    CaptureRuntimeVersion();
    CheckRequiredExports();
    CheckTrackedGaps();
}
