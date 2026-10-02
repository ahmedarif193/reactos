/*
 * PROJECT:     LiberNT WoW64 layer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Thunks for ReactOS syscalls that have no Wine counterpart
 * COPYRIGHT:   Copyright 2026 LiberNT Team
 */

#ifndef __WOW64_ROSSYSCALL_H
#define __WOW64_ROSSYSCALL_H

/* Syscalls present in the ReactOS syscall table (sysfuncs.h) but absent from
 * Wine's ALL_SYSCALLS32 list.  Each entry has a wow64_* thunk implemented in
 * rossyscall.c (or sync.c for the LPC ones). */
#define ALL_REACTOS_SYSCALLS \
    ROS_SYSCALL_ENTRY( NtAccessCheckByType ) \
    ROS_SYSCALL_ENTRY( NtAccessCheckByTypeResultList ) \
    ROS_SYSCALL_ENTRY( NtAccessCheckByTypeResultListAndAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtAccessCheckByTypeResultListAndAuditAlarmByHandle ) \
    ROS_SYSCALL_ENTRY( NtAddBootEntry ) \
    ROS_SYSCALL_ENTRY( NtAddDriverEntry ) \
    ROS_SYSCALL_ENTRY( NtAllocateUserPhysicalPages ) \
    ROS_SYSCALL_ENTRY( NtAlpcCancelMessage ) \
    ROS_SYSCALL_ENTRY( NtAlpcConnectPortEx ) \
    ROS_SYSCALL_ENTRY( NtAlpcCreatePortSection ) \
    ROS_SYSCALL_ENTRY( NtAlpcCreateResourceReserve ) \
    ROS_SYSCALL_ENTRY( NtAlpcCreateSectionView ) \
    ROS_SYSCALL_ENTRY( NtAlpcCreateSecurityContext ) \
    ROS_SYSCALL_ENTRY( NtAlpcDeletePortSection ) \
    ROS_SYSCALL_ENTRY( NtAlpcDeleteResourceReserve ) \
    ROS_SYSCALL_ENTRY( NtAlpcDeleteSectionView ) \
    ROS_SYSCALL_ENTRY( NtAlpcDeleteSecurityContext ) \
    ROS_SYSCALL_ENTRY( NtAlpcImpersonateClientContainerOfPort ) \
    ROS_SYSCALL_ENTRY( NtAlpcOpenSenderProcess ) \
    ROS_SYSCALL_ENTRY( NtAlpcOpenSenderThread ) \
    ROS_SYSCALL_ENTRY( NtAlpcQueryInformation ) \
    ROS_SYSCALL_ENTRY( NtAlpcQueryInformationMessage ) \
    ROS_SYSCALL_ENTRY( NtAlpcRevokeSecurityContext ) \
    ROS_SYSCALL_ENTRY( NtAlpcSetInformation ) \
    ROS_SYSCALL_ENTRY( NtAssociateWaitCompletionPacket ) \
    ROS_SYSCALL_ENTRY( NtCancelDeviceWakeupRequest ) \
    ROS_SYSCALL_ENTRY( NtCancelWaitCompletionPacket ) \
    ROS_SYSCALL_ENTRY( NtCommitComplete ) \
    ROS_SYSCALL_ENTRY( NtCommitEnlistment ) \
    ROS_SYSCALL_ENTRY( NtCompactKeys ) \
    ROS_SYSCALL_ENTRY( NtCompressKey ) \
    ROS_SYSCALL_ENTRY( NtCreateEnlistment ) \
    ROS_SYSCALL_ENTRY( NtCreateEventPair ) \
    ROS_SYSCALL_ENTRY( NtCreateJobSet ) \
    ROS_SYSCALL_ENTRY( NtCreatePrivateNamespace ) \
    ROS_SYSCALL_ENTRY( NtCreateProcess ) \
    ROS_SYSCALL_ENTRY( NtCreateProfile ) \
    ROS_SYSCALL_ENTRY( NtCreateResourceManager ) \
    ROS_SYSCALL_ENTRY( NtCreateTransactionManager ) \
    ROS_SYSCALL_ENTRY( NtCreateWaitCompletionPacket ) \
    ROS_SYSCALL_ENTRY( NtCreateWaitablePort ) \
    ROS_SYSCALL_ENTRY( NtCreateWnfStateName ) \
    ROS_SYSCALL_ENTRY( NtDeleteBootEntry ) \
    ROS_SYSCALL_ENTRY( NtDeleteDriverEntry ) \
    ROS_SYSCALL_ENTRY( NtDeleteObjectAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtDeletePrivateNamespace ) \
    ROS_SYSCALL_ENTRY( NtDeleteWnfStateData ) \
    ROS_SYSCALL_ENTRY( NtDeleteWnfStateName ) \
    ROS_SYSCALL_ENTRY( NtEnumerateBootEntries ) \
    ROS_SYSCALL_ENTRY( NtEnumerateDriverEntries ) \
    ROS_SYSCALL_ENTRY( NtEnumerateSystemEnvironmentValuesEx ) \
    ROS_SYSCALL_ENTRY( NtEnumerateTransactionObject ) \
    ROS_SYSCALL_ENTRY( NtExtendSection ) \
    ROS_SYSCALL_ENTRY( NtFlushWriteBuffer ) \
    ROS_SYSCALL_ENTRY( NtFreeUserPhysicalPages ) \
    ROS_SYSCALL_ENTRY( NtGetCurrentProcessorNumberEx ) \
    ROS_SYSCALL_ENTRY( NtGetDevicePowerState ) \
    ROS_SYSCALL_ENTRY( NtGetNotificationResourceManager ) \
    ROS_SYSCALL_ENTRY( NtGetPlugPlayEvent ) \
    ROS_SYSCALL_ENTRY( NtImpersonateThread ) \
    ROS_SYSCALL_ENTRY( NtInitializeRegistry ) \
    ROS_SYSCALL_ENTRY( NtIsSystemResumeAutomatic ) \
    ROS_SYSCALL_ENTRY( NtLockProductActivationKeys ) \
    ROS_SYSCALL_ENTRY( NtLockRegistryKey ) \
    ROS_SYSCALL_ENTRY( NtMapUserPhysicalPages ) \
    ROS_SYSCALL_ENTRY( NtModifyBootEntry ) \
    ROS_SYSCALL_ENTRY( NtModifyDriverEntry ) \
    ROS_SYSCALL_ENTRY( NtNotifyChangeDirectoryFileEx ) \
    ROS_SYSCALL_ENTRY( NtOpenEnlistment ) \
    ROS_SYSCALL_ENTRY( NtOpenEventPair ) \
    ROS_SYSCALL_ENTRY( NtOpenObjectAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtOpenPrivateNamespace ) \
    ROS_SYSCALL_ENTRY( NtOpenResourceManager ) \
    ROS_SYSCALL_ENTRY( NtOpenTransaction ) \
    ROS_SYSCALL_ENTRY( NtOpenTransactionManager ) \
    ROS_SYSCALL_ENTRY( NtPlugPlayControl ) \
    ROS_SYSCALL_ENTRY( NtPrePrepareComplete ) \
    ROS_SYSCALL_ENTRY( NtPrePrepareEnlistment ) \
    ROS_SYSCALL_ENTRY( NtPrepareComplete ) \
    ROS_SYSCALL_ENTRY( NtPrepareEnlistment ) \
    ROS_SYSCALL_ENTRY( NtPrivilegeObjectAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtPrivilegedServiceAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtPropagationComplete ) \
    ROS_SYSCALL_ENTRY( NtPropagationFailed ) \
    ROS_SYSCALL_ENTRY( NtQueryBootEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtQueryBootOptions ) \
    ROS_SYSCALL_ENTRY( NtQueryDebugFilterState ) \
    ROS_SYSCALL_ENTRY( NtQueryDirectoryFileEx ) \
    ROS_SYSCALL_ENTRY( NtQueryDriverEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationByName ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationEnlistment ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationPort ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationResourceManager ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationTransaction ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationTransactionManager ) \
    ROS_SYSCALL_ENTRY( NtQueryIntervalProfile ) \
    ROS_SYSCALL_ENTRY( NtQueryOpenSubKeys ) \
    ROS_SYSCALL_ENTRY( NtQueryOpenSubKeysEx ) \
    ROS_SYSCALL_ENTRY( NtQueryPortInformationProcess ) \
    ROS_SYSCALL_ENTRY( NtQueryQuotaInformationFile ) \
    ROS_SYSCALL_ENTRY( NtQueryWnfStateData ) \
    ROS_SYSCALL_ENTRY( NtQueryWnfStateNameInformation ) \
    ROS_SYSCALL_ENTRY( NtReadOnlyEnlistment ) \
    ROS_SYSCALL_ENTRY( NtRecoverEnlistment ) \
    ROS_SYSCALL_ENTRY( NtRecoverResourceManager ) \
    ROS_SYSCALL_ENTRY( NtRecoverTransactionManager ) \
    ROS_SYSCALL_ENTRY( NtRegisterProtocolAddressInformation ) \
    ROS_SYSCALL_ENTRY( NtRenameTransactionManager ) \
    ROS_SYSCALL_ENTRY( NtReplyWaitReplyPort ) \
    ROS_SYSCALL_ENTRY( NtRequestDeviceWakeup ) \
    ROS_SYSCALL_ENTRY( NtRequestPort ) \
    ROS_SYSCALL_ENTRY( NtRequestWakeupLatency ) \
    ROS_SYSCALL_ENTRY( NtRollbackComplete ) \
    ROS_SYSCALL_ENTRY( NtRollbackEnlistment ) \
    ROS_SYSCALL_ENTRY( NtRollforwardTransactionManager ) \
    ROS_SYSCALL_ENTRY( NtSaveKeyEx ) \
    ROS_SYSCALL_ENTRY( NtSaveMergedKeys ) \
    ROS_SYSCALL_ENTRY( NtSetBootEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtSetBootOptions ) \
    ROS_SYSCALL_ENTRY( NtSetDefaultHardErrorPort ) \
    ROS_SYSCALL_ENTRY( NtSetDriverEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtSetHighEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetHighWaitLowEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetInformationEnlistment ) \
    ROS_SYSCALL_ENTRY( NtSetInformationResourceManager ) \
    ROS_SYSCALL_ENTRY( NtSetInformationTransaction ) \
    ROS_SYSCALL_ENTRY( NtSetInformationTransactionManager ) \
    ROS_SYSCALL_ENTRY( NtSetLowEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetLowWaitHighEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetQuotaInformationFile ) \
    ROS_SYSCALL_ENTRY( NtSetSystemEnvironmentValue ) \
    ROS_SYSCALL_ENTRY( NtSetSystemEnvironmentValueEx ) \
    ROS_SYSCALL_ENTRY( NtSetSystemPowerState ) \
    ROS_SYSCALL_ENTRY( NtSetUuidSeed ) \
    ROS_SYSCALL_ENTRY( NtSinglePhaseReject ) \
    ROS_SYSCALL_ENTRY( NtStartProfile ) \
    ROS_SYSCALL_ENTRY( NtStopProfile ) \
    ROS_SYSCALL_ENTRY( NtSubscribeWnfStateChange ) \
    ROS_SYSCALL_ENTRY( NtTranslateFilePath ) \
    ROS_SYSCALL_ENTRY( NtUnloadKey2 ) \
    ROS_SYSCALL_ENTRY( NtUnloadKeyEx ) \
    ROS_SYSCALL_ENTRY( NtUnsubscribeWnfStateChange ) \
    ROS_SYSCALL_ENTRY( NtUpdateWnfStateData ) \
    ROS_SYSCALL_ENTRY( NtVdmControl ) \
    ROS_SYSCALL_ENTRY( NtWaitHighEventPair ) \
    ROS_SYSCALL_ENTRY( NtWaitLowEventPair )

#define ROS_SYSCALL_ENTRY(name) extern NTSTATUS WINAPI wow64_ ## name( UINT *args );
ALL_REACTOS_SYSCALLS
#undef ROS_SYSCALL_ENTRY

#endif /* __WOW64_ROSSYSCALL_H */
