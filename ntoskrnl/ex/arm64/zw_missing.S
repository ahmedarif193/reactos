/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 Missing System Call Stubs
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

.text

/* Add missing Zw* function stubs */

.global ZwAccessCheckAndAuditAlarm
ZwAccessCheckAndAuditAlarm:
    mov w8, #0
    svc #0
    ret

.global ZwAddBootEntry
ZwAddBootEntry:
    mov w8, #279
    svc #0
    ret

.global ZwAddDriverEntry
ZwAddDriverEntry:
    mov w8, #280
    svc #0
    ret

.global ZwAlertThread
ZwAlertThread:
    mov w8, #9
    svc #0
    ret

.global ZwAssignProcessToJobObject
ZwAssignProcessToJobObject:
    mov w8, #1
    svc #0
    ret

.global ZwCancelIoFile
ZwCancelIoFile:
    mov w8, #14
    svc #0
    ret

.global ZwCancelTimer
ZwCancelTimer:
    mov w8, #17
    svc #0
    ret

.global ZwClearEvent
ZwClearEvent:
    mov w8, #22
    svc #0
    ret

.global ZwCloseObjectAuditAlarm
ZwCloseObjectAuditAlarm:
    mov w8, #16
    svc #0
    ret

.global ZwCreateJobObject
ZwCreateJobObject:
    mov w8, #39
    svc #0
    ret

.global ZwCreateTimer
ZwCreateTimer:
    mov w8, #51
    svc #0
    ret

.global ZwDeleteBootEntry
ZwDeleteBootEntry:
    mov w8, #281
    svc #0
    ret

.global ZwDeleteDriverEntry
ZwDeleteDriverEntry:
    mov w8, #282
    svc #0
    ret

.global ZwDeleteFile
ZwDeleteFile:
    mov w8, #61
    svc #0
    ret

.global ZwDuplicateToken
ZwDuplicateToken:
    mov w8, #66
    svc #0
    ret

.global ZwEnumerateBootEntries
ZwEnumerateBootEntries:
    mov w8, #283
    svc #0
    ret

.global ZwEnumerateDriverEntries
ZwEnumerateDriverEntries:
    mov w8, #284
    svc #0
    ret

.global ZwFlushVirtualMemory
ZwFlushVirtualMemory:
    mov w8, #82
    svc #0
    ret

.global ZwInitiatePowerAction
ZwInitiatePowerAction:
    mov w8, #87
    svc #0
    ret

.global ZwIsProcessInJob
ZwIsProcessInJob:
    mov w8, #88
    svc #0
    ret

.global ZwLoadKey
ZwLoadKey:
    mov w8, #95
    svc #0
    ret

.global ZwModifyBootEntry
ZwModifyBootEntry:
    mov w8, #285
    svc #0
    ret

.global ZwModifyDriverEntry
ZwModifyDriverEntry:
    mov w8, #286
    svc #0
    ret

.global ZwNotifyChangeKey
ZwNotifyChangeKey:
    mov w8, #106
    svc #0
    ret

.global ZwOpenJobObject
ZwOpenJobObject:
    mov w8, #112
    svc #0
    ret

.global ZwOpenProcess
ZwOpenProcess:
    mov w8, #122
    svc #0
    ret

.global ZwOpenSection
ZwOpenSection:
    mov w8, #128
    svc #0
    ret

.global ZwOpenThread
ZwOpenThread:
    mov w8, #129
    svc #0
    ret

.global ZwOpenThreadToken
ZwOpenThreadToken:
    mov w8, #130
    svc #0
    ret

.global ZwOpenTimer
ZwOpenTimer:
    mov w8, #131
    svc #0
    ret

.global ZwPowerInformation
ZwPowerInformation:
    mov w8, #137
    svc #0
    ret

.global ZwPulseEvent
ZwPulseEvent:
    mov w8, #141
    svc #0
    ret

.global ZwQueryBootEntryOrder
ZwQueryBootEntryOrder:
    mov w8, #287
    svc #0
    ret

.global ZwQueryBootOptions
ZwQueryBootOptions:
    mov w8, #288
    svc #0
    ret

.global ZwQueryDefaultUILanguage
ZwQueryDefaultUILanguage:
    mov w8, #149
    svc #0
    ret

.global ZwQueryDirectoryFile
ZwQueryDirectoryFile:
    mov w8, #150
    svc #0
    ret

.global ZwQueryDriverEntryOrder
ZwQueryDriverEntryOrder:
    mov w8, #289
    svc #0
    ret

.global ZwQueryEaFile
ZwQueryEaFile:
    mov w8, #152
    svc #0
    ret

.global ZwQueryFullAttributesFile
ZwQueryFullAttributesFile:
    mov w8, #153
    svc #0
    ret

.global ZwQueryInformationJobObject
ZwQueryInformationJobObject:
    mov w8, #163
    svc #0
    ret

.global ZwQueryInstallUILanguage
ZwQueryInstallUILanguage:
    mov w8, #166
    svc #0
    ret

.global ZwReplaceKey
ZwReplaceKey:
    mov w8, #195
    svc #0
    ret

.global ZwResetEvent
ZwResetEvent:
    mov w8, #196
    svc #0
    ret

.global ZwRestoreKey
ZwRestoreKey:
    mov w8, #197
    svc #0
    ret

.global ZwSaveKey
ZwSaveKey:
    mov w8, #202
    svc #0
    ret

.global ZwSaveKeyEx
ZwSaveKeyEx:
    mov w8, #203
    svc #0
    ret

.global ZwSecureConnectPort
ZwSecureConnectPort:
    mov w8, #204
    svc #0
    ret

.global ZwSetBootEntryOrder
ZwSetBootEntryOrder:
    mov w8, #290
    svc #0
    ret

.global ZwSetBootOptions
ZwSetBootOptions:
    mov w8, #291
    svc #0
    ret

.global ZwSetDefaultLocale
ZwSetDefaultLocale:
    mov w8, #207
    svc #0
    ret

.global ZwSetDefaultUILanguage
ZwSetDefaultUILanguage:
    mov w8, #209
    svc #0
    ret

.global ZwSetDriverEntryOrder
ZwSetDriverEntryOrder:
    mov w8, #292
    svc #0
    ret

.global ZwSetEaFile
ZwSetEaFile:
    mov w8, #212
    svc #0
    ret

.global ZwSetEvent
ZwSetEvent:
    mov w8, #213
    svc #0
    ret

.global ZwSetInformationJobObject
ZwSetInformationJobObject:
    mov w8, #226
    svc #0
    ret

.global ZwSetInformationObject
ZwSetInformationObject:
    mov w8, #227
    svc #0
    ret

.global ZwSetTimer
ZwSetTimer:
    mov w8, #248
    svc #0
    ret

.global ZwSetVolumeInformationFile
ZwSetVolumeInformationFile:
    mov w8, #252
    svc #0
    ret

.global ZwTerminateJobObject
ZwTerminateJobObject:
    mov w8, #258
    svc #0
    ret

.global ZwTranslateFilePath
ZwTranslateFilePath:
    mov w8, #293
    svc #0
    ret

.global ZwUnloadDriver
ZwUnloadDriver:
    mov w8, #264
    svc #0
    ret

.global ZwUnloadKey
ZwUnloadKey:
    mov w8, #265
    svc #0
    ret

.global ZwWaitForMultipleObjects
ZwWaitForMultipleObjects:
    mov w8, #269
    svc #0
    ret

.global ZwYieldExecution
ZwYieldExecution:
    mov w8, #278
    svc #0
    ret

/* Add missing non-Zw functions */

.global RtlUnwind
RtlUnwind:
    /* Simple stub for now */
    ret

.global KeSaveStateForHibernate
KeSaveStateForHibernate:
    /* Stub for hibernation - STATUS_NOT_IMPLEMENTED */
    movn x0, #0x3FFD
    movk x0, #0xC000, lsl #16
    ret

.global KeSetDmaIoCoherency
KeSetDmaIoCoherency:
    /* Stub for DMA coherency */
    ret

.global KeSynchronizeExecution
KeSynchronizeExecution:
    /* Stub for synchronization */
    mov x0, #0  /* FALSE */
    ret