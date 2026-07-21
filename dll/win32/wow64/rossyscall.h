/*
 * PROJECT:     ReactOS WoW64 layer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Thunks for ReactOS syscalls that have no Wine counterpart
 * COPYRIGHT:   Copyright 2026 ReactOS Team
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
    ROS_SYSCALL_ENTRY( NtCancelDeviceWakeupRequest ) \
    ROS_SYSCALL_ENTRY( NtCompactKeys ) \
    ROS_SYSCALL_ENTRY( NtCompressKey ) \
    ROS_SYSCALL_ENTRY( NtCreateEventPair ) \
    ROS_SYSCALL_ENTRY( NtCreateJobSet ) \
    ROS_SYSCALL_ENTRY( NtCreateProcess ) \
    ROS_SYSCALL_ENTRY( NtCreateProfile ) \
    ROS_SYSCALL_ENTRY( NtCreateWaitablePort ) \
    ROS_SYSCALL_ENTRY( NtCreateWnfStateName ) \
    ROS_SYSCALL_ENTRY( NtDeleteBootEntry ) \
    ROS_SYSCALL_ENTRY( NtDeleteDriverEntry ) \
    ROS_SYSCALL_ENTRY( NtDeleteObjectAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtDeleteWnfStateData ) \
    ROS_SYSCALL_ENTRY( NtDeleteWnfStateName ) \
    ROS_SYSCALL_ENTRY( NtEnumerateBootEntries ) \
    ROS_SYSCALL_ENTRY( NtEnumerateDriverEntries ) \
    ROS_SYSCALL_ENTRY( NtEnumerateSystemEnvironmentValuesEx ) \
    ROS_SYSCALL_ENTRY( NtExtendSection ) \
    ROS_SYSCALL_ENTRY( NtFlushWriteBuffer ) \
    ROS_SYSCALL_ENTRY( NtFreeUserPhysicalPages ) \
    ROS_SYSCALL_ENTRY( NtGetCurrentProcessorNumberEx ) \
    ROS_SYSCALL_ENTRY( NtGetDevicePowerState ) \
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
    ROS_SYSCALL_ENTRY( NtOpenEventPair ) \
    ROS_SYSCALL_ENTRY( NtOpenObjectAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtPlugPlayControl ) \
    ROS_SYSCALL_ENTRY( NtPrivilegeObjectAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtPrivilegedServiceAuditAlarm ) \
    ROS_SYSCALL_ENTRY( NtQueryBootEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtQueryBootOptions ) \
    ROS_SYSCALL_ENTRY( NtQueryDebugFilterState ) \
    ROS_SYSCALL_ENTRY( NtQueryDirectoryFileEx ) \
    ROS_SYSCALL_ENTRY( NtQueryDriverEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationByName ) \
    ROS_SYSCALL_ENTRY( NtQueryInformationPort ) \
    ROS_SYSCALL_ENTRY( NtQueryIntervalProfile ) \
    ROS_SYSCALL_ENTRY( NtQueryOpenSubKeys ) \
    ROS_SYSCALL_ENTRY( NtQueryOpenSubKeysEx ) \
    ROS_SYSCALL_ENTRY( NtQueryPortInformationProcess ) \
    ROS_SYSCALL_ENTRY( NtQueryQuotaInformationFile ) \
    ROS_SYSCALL_ENTRY( NtQueryWnfStateData ) \
    ROS_SYSCALL_ENTRY( NtQueryWnfStateNameInformation ) \
    ROS_SYSCALL_ENTRY( NtReplyWaitReplyPort ) \
    ROS_SYSCALL_ENTRY( NtRequestDeviceWakeup ) \
    ROS_SYSCALL_ENTRY( NtRequestPort ) \
    ROS_SYSCALL_ENTRY( NtRequestWakeupLatency ) \
    ROS_SYSCALL_ENTRY( NtSaveKeyEx ) \
    ROS_SYSCALL_ENTRY( NtSaveMergedKeys ) \
    ROS_SYSCALL_ENTRY( NtSetBootEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtSetBootOptions ) \
    ROS_SYSCALL_ENTRY( NtSetDefaultHardErrorPort ) \
    ROS_SYSCALL_ENTRY( NtSetDriverEntryOrder ) \
    ROS_SYSCALL_ENTRY( NtSetHighEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetHighWaitLowEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetLowEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetLowWaitHighEventPair ) \
    ROS_SYSCALL_ENTRY( NtSetQuotaInformationFile ) \
    ROS_SYSCALL_ENTRY( NtSetSystemEnvironmentValue ) \
    ROS_SYSCALL_ENTRY( NtSetSystemEnvironmentValueEx ) \
    ROS_SYSCALL_ENTRY( NtSetSystemPowerState ) \
    ROS_SYSCALL_ENTRY( NtSetUuidSeed ) \
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
