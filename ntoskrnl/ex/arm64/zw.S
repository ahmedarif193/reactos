/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 System Call Stubs
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

.text

/* 
 * Minimal ARM64 syscall stub implementation
 * This is a placeholder that should be generated from syscalls
 */

/* System call table */
.data
.global KiServiceTable
KiServiceTable:
    /* This should be generated from syscalls.inc */
    /* For now, provide a minimal table with 280 entries */
    .rept 280
    .quad KiZwSystemService  /* All point to the stub handler for now */
    .endr

.text
.global KiZwSystemService
KiZwSystemService:
    /* System service dispatcher for ARM64 */
    /* For now, just return STATUS_NOT_IMPLEMENTED */
    movn x0, #0x3FFD    /* Load ~0x3FFD = 0xFFFFFFFFFFFFC002 */
    movk x0, #0x3FFF, lsl #16  /* Clear upper bits to get 0xC0000002 */
    ret

/* Provide a few essential Zw stubs for kernel initialization */

.global ZwClose
ZwClose:
    mov w8, #15  /* NtClose syscall number */
    svc #0
    ret

.global ZwCreateKey
ZwCreateKey:
    mov w8, #29  /* NtCreateKey syscall number */
    svc #0
    ret

.global ZwOpenKey
ZwOpenKey:
    mov w8, #119  /* NtOpenKey syscall number */
    svc #0
    ret

.global ZwSetValueKey
ZwSetValueKey:
    mov w8, #247  /* NtSetValueKey syscall number */
    svc #0
    ret

.global ZwOpenFile
ZwOpenFile:
    mov w8, #116  /* NtOpenFile syscall number */
    svc #0
    ret

.global ZwDeviceIoControlFile
ZwDeviceIoControlFile:
    mov w8, #7  /* NtDeviceIoControlFile syscall number */
    svc #0
    ret

.global ZwQueryValueKey
ZwQueryValueKey:
    mov w8, #177  /* NtQueryValueKey syscall number */
    svc #0
    ret

.global ZwDeleteKey
ZwDeleteKey:
    mov w8, #69  /* NtDeleteKey syscall number */
    svc #0
    ret

.global NtCallbackReturn
NtCallbackReturn:
    mov w8, #4  /* NtCallbackReturn syscall number */
    svc #0
    ret

.global ZwEnumerateKey
ZwEnumerateKey:
    mov w8, #71  /* NtEnumerateKey syscall number */
    svc #0
    ret

.global ZwFreeVirtualMemory
ZwFreeVirtualMemory:
    mov w8, #83  /* NtFreeVirtualMemory syscall number */
    svc #0
    ret

.global ZwQueryInformationThread
ZwQueryInformationThread:
    mov w8, #167  /* NtQueryInformationThread syscall number */
    svc #0
    ret

.global ZwSetInformationThread
ZwSetInformationThread:
    mov w8, #235  /* NtSetInformationThread syscall number */
    svc #0
    ret

.global ZwCreateThread
ZwCreateThread:
    mov w8, #52  /* NtCreateThread syscall number */
    svc #0
    ret

.global ZwQuerySystemInformation
ZwQuerySystemInformation:
    mov w8, #173  /* NtQuerySystemInformation syscall number */
    svc #0
    ret

.global ZwReadFile
ZwReadFile:
    mov w8, #187  /* NtReadFile syscall number */
    svc #0
    ret

.global ZwSetInformationFile
ZwSetInformationFile:
    mov w8, #224  /* NtSetInformationFile syscall number */
    svc #0
    ret

.global ZwAllocateVirtualMemory
ZwAllocateVirtualMemory:
    mov w8, #24  /* NtAllocateVirtualMemory syscall number */
    svc #0
    ret

.global ZwProtectVirtualMemory
ZwProtectVirtualMemory:
    mov w8, #80  /* NtProtectVirtualMemory syscall number */
    svc #0
    ret

.global ZwSetSecurityObject
ZwSetSecurityObject:
    mov w8, #246  /* NtSetSecurityObject syscall number */
    svc #0
    ret

.global ZwAdjustPrivilegesToken
ZwAdjustPrivilegesToken:
    mov w8, #20  /* NtAdjustPrivilegesToken syscall number */
    svc #0
    ret

.global ZwCreateFile
ZwCreateFile:
    mov w8, #55  /* NtCreateFile syscall number */
    svc #0
    ret

.global ZwQuerySecurityObject
ZwQuerySecurityObject:
    mov w8, #171  /* NtQuerySecurityObject syscall number */
    svc #0
    ret

.global ZwEnumerateValueKey
ZwEnumerateValueKey:
    mov w8, #76  /* NtEnumerateValueKey syscall number */
    svc #0
    ret

.global ZwDeleteValueKey
ZwDeleteValueKey:
    mov w8, #70  /* NtDeleteValueKey syscall number */
    svc #0
    ret

.global ZwOpenProcessToken
ZwOpenProcessToken:
    mov w8, #123  /* NtOpenProcessToken syscall number */
    svc #0
    ret

.global ZwDuplicateObject
ZwDuplicateObject:
    mov w8, #68  /* NtDuplicateObject syscall number */
    svc #0
    ret

.global ZwQueryInformationProcess
ZwQueryInformationProcess:
    mov w8, #165  /* NtQueryInformationProcess syscall number */
    svc #0
    ret

.global ZwSetInformationProcess
ZwSetInformationProcess:
    mov w8, #223  /* NtSetInformationProcess syscall number */
    svc #0
    ret

.global ZwOpenThreadTokenEx
ZwOpenThreadTokenEx:
    mov w8, #125  /* NtOpenThreadTokenEx syscall number */
    svc #0
    ret

.global ZwQueryInformationToken
ZwQueryInformationToken:
    mov w8, #168  /* NtQueryInformationToken syscall number */
    svc #0
    ret

.global ZwOpenProcessTokenEx
ZwOpenProcessTokenEx:
    mov w8, #124  /* NtOpenProcessTokenEx syscall number */
    svc #0
    ret

.global ZwCreateSection
ZwCreateSection:
    mov w8, #47  /* NtCreateSection syscall number */
    svc #0
    ret

.global ZwWriteVirtualMemory
ZwWriteVirtualMemory:
    mov w8, #277  /* NtWriteVirtualMemory syscall number */
    svc #0
    ret

.global ZwCreateProcess
ZwCreateProcess:
    mov w8, #44  /* NtCreateProcess syscall number */
    svc #0
    ret

.global ZwQuerySection
ZwQuerySection:
    mov w8, #172  /* NtQuerySection syscall number */
    svc #0
    ret

.global ZwQueryVirtualMemory
ZwQueryVirtualMemory:
    mov w8, #178  /* NtQueryVirtualMemory syscall number */
    svc #0
    ret

.global ZwQueryPerformanceCounter
ZwQueryPerformanceCounter:
    mov w8, #166  /* NtQueryPerformanceCounter syscall number */
    svc #0
    ret

.global ZwRaiseException
ZwRaiseException:
    mov w8, #181  /* NtRaiseException syscall number */
    svc #0
    ret

.global ZwContinue
ZwContinue:
    mov w8, #40  /* NtContinue syscall number */
    svc #0
    ret

.global DebugService2
DebugService2:
    /* ARM64 debug service implementation */
    mov w8, #0x2D  /* Debug service syscall number */
    svc #0
    ret

.global DebugService
DebugService:
    /* ARM64 debug service implementation - same as DebugService2 */
    mov w8, #0x2D  /* Debug service syscall number */
    svc #0
    ret

.global ZwWriteFile
ZwWriteFile:
    mov w8, #278  /* NtWriteFile syscall number */
    svc #0
    ret

.global ZwFlushBuffersFile
ZwFlushBuffersFile:
    mov w8, #82  /* NtFlushBuffersFile syscall number */
    svc #0
    ret

.global ZwOpenEvent
ZwOpenEvent:
    mov w8, #114  /* NtOpenEvent syscall number */
    svc #0
    ret

.global ZwCreateEvent
ZwCreateEvent:
    mov w8, #48  /* NtCreateEvent syscall number */
    svc #0
    ret

.global ZwWaitForSingleObject
ZwWaitForSingleObject:
    mov w8, #267  /* NtWaitForSingleObject syscall number */
    svc #0
    ret

.global ZwConnectPort
ZwConnectPort:
    mov w8, #39  /* NtConnectPort syscall number */
    svc #0
    ret

.global ZwTerminateThread
ZwTerminateThread:
    mov w8, #253  /* NtTerminateThread syscall number */
    svc #0
    ret

.global ZwTerminateProcess
ZwTerminateProcess:
    mov w8, #252  /* NtTerminateProcess syscall number */
    svc #0
    ret

.global ZwRequestWaitReplyPort
ZwRequestWaitReplyPort:
    mov w8, #199  /* NtRequestWaitReplyPort syscall number */
    svc #0
    ret

.global ZwQueryInformationFile
ZwQueryInformationFile:
    mov w8, #164  /* NtQueryInformationFile syscall number */
    svc #0
    ret

.global ZwCompleteConnectPort
ZwCompleteConnectPort:
    mov w8, #38  /* NtCompleteConnectPort syscall number */
    svc #0
    ret

.global ZwCreateDirectoryObject
ZwCreateDirectoryObject:
    mov w8, #43  /* NtCreateDirectoryObject syscall number */
    svc #0
    ret

.global ZwCreateSymbolicLinkObject
ZwCreateSymbolicLinkObject:
    mov w8, #53  /* NtCreateSymbolicLinkObject syscall number */
    svc #0
    ret

.global ZwOpenDirectoryObject
ZwOpenDirectoryObject:
    mov w8, #113  /* NtOpenDirectoryObject syscall number */
    svc #0
    ret

.global ZwQueryDirectoryObject
ZwQueryDirectoryObject:
    mov w8, #159  /* NtQueryDirectoryObject syscall number */
    svc #0
    ret

.global ZwOpenSymbolicLinkObject
ZwOpenSymbolicLinkObject:
    mov w8, #127  /* NtOpenSymbolicLinkObject syscall number */
    svc #0
    ret

.global ZwMakeTemporaryObject
ZwMakeTemporaryObject:
    mov w8, #104  /* NtMakeTemporaryObject syscall number */
    svc #0
    ret

.global ZwListenPort
ZwListenPort:
    mov w8, #102  /* NtListenPort syscall number */
    svc #0
    ret

.global ZwAcceptConnectPort
ZwAcceptConnectPort:
    mov w8, #2  /* NtAcceptConnectPort syscall number */
    svc #0
    ret

.global ZwCreatePort
ZwCreatePort:
    mov w8, #46  /* NtCreatePort syscall number */
    svc #0
    ret

.global ZwReplyWaitReceivePort
ZwReplyWaitReceivePort:
    mov w8, #190  /* NtReplyWaitReceivePort syscall number */
    svc #0
    ret

.global ZwSetTimerResolution
ZwSetTimerResolution:
    mov w8, #244  /* NtSetTimerResolution syscall number */
    svc #0
    ret

.global ZwFlushKey
ZwFlushKey:
    mov w8, #81  /* NtFlushKey syscall number */
    svc #0
    ret

.global ZwSetSystemPowerState
ZwSetSystemPowerState:
    mov w8, #242  /* NtSetSystemPowerState syscall number */
    svc #0
    ret

.global ZwShutdownSystem
ZwShutdownSystem:
    mov w8, #209  /* NtShutdownSystem syscall number */
    svc #0
    ret

.global ZwCreateKeyedEvent
ZwCreateKeyedEvent:
    mov w8, #50  /* NtCreateKeyedEvent syscall number */
    svc #0
    ret

.global ZwDisplayString
ZwDisplayString:
    mov w8, #66  /* NtDisplayString syscall number */
    svc #0
    ret

.global ZwFlushInstructionCache
ZwFlushInstructionCache:
    mov w8, #84  /* NtFlushInstructionCache syscall number */
    svc #0
    ret

.global ZwFsControlFile
ZwFsControlFile:
    mov w8, #90  /* NtFsControlFile syscall number */
    svc #0
    ret

.global ZwInitializeRegistry
ZwInitializeRegistry:
    mov w8, #98  /* NtInitializeRegistry syscall number */
    svc #0
    ret

.global ZwLoadDriver
ZwLoadDriver:
    mov w8, #103  /* NtLoadDriver syscall number */
    svc #0
    ret

.global ZwMapViewOfSection
ZwMapViewOfSection:
    mov w8, #106  /* NtMapViewOfSection syscall number */
    svc #0
    ret

.global ZwQueryDefaultLocale
ZwQueryDefaultLocale:
    mov w8, #158  /* NtQueryDefaultLocale syscall number */
    svc #0
    ret

.global ZwQueryKey
ZwQueryKey:
    mov w8, #169  /* NtQueryKey syscall number */
    svc #0
    ret

.global ZwQueryObject
ZwQueryObject:
    mov w8, #170  /* NtQueryObject syscall number */
    svc #0
    ret

.global ZwQuerySymbolicLinkObject
ZwQuerySymbolicLinkObject:
    mov w8, #174  /* NtQuerySymbolicLinkObject syscall number */
    svc #0
    ret

.global ZwQueryVolumeInformationFile
ZwQueryVolumeInformationFile:
    mov w8, #179  /* NtQueryVolumeInformationFile syscall number */
    svc #0
    ret

.global ZwRequestPort
ZwRequestPort:
    mov w8, #198  /* NtRequestPort syscall number */
    svc #0
    ret

.global ZwResumeThread
ZwResumeThread:
    mov w8, #206  /* NtResumeThread syscall number */
    svc #0
    ret

.global ZwSetSystemInformation
ZwSetSystemInformation:
    mov w8, #241  /* NtSetSystemInformation syscall number */
    svc #0
    ret

.global ZwSetSystemTime
ZwSetSystemTime:
    mov w8, #243  /* NtSetSystemTime syscall number */
    svc #0
    ret

.global ZwUnmapViewOfSection
ZwUnmapViewOfSection:
    mov w8, #263  /* NtUnmapViewOfSection syscall number */
    svc #0
    ret

.global RtlCaptureContext
RtlCaptureContext:
    /* Save all general purpose registers */
    str x0, [x0, #0x8]   /* X0 */
    str x1, [x0, #0x10]  /* X1 */
    str x2, [x0, #0x18]  /* X2 */
    str x3, [x0, #0x20]  /* X3 */
    str x4, [x0, #0x28]  /* X4 */
    str x5, [x0, #0x30]  /* X5 */
    str x6, [x0, #0x38]  /* X6 */
    str x7, [x0, #0x40]  /* X7 */
    str x8, [x0, #0x48]  /* X8 */
    str x9, [x0, #0x50]  /* X9 */
    str x10, [x0, #0x58] /* X10 */
    str x11, [x0, #0x60] /* X11 */
    str x12, [x0, #0x68] /* X12 */
    str x13, [x0, #0x70] /* X13 */
    str x14, [x0, #0x78] /* X14 */
    str x15, [x0, #0x80] /* X15 */
    str x16, [x0, #0x88] /* X16 */
    str x17, [x0, #0x90] /* X17 */
    str x18, [x0, #0x98] /* X18 */
    str x19, [x0, #0xA0] /* X19 */
    str x20, [x0, #0xA8] /* X20 */
    str x21, [x0, #0xB0] /* X21 */
    str x22, [x0, #0xB8] /* X22 */
    str x23, [x0, #0xC0] /* X23 */
    str x24, [x0, #0xC8] /* X24 */
    str x25, [x0, #0xD0] /* X25 */
    str x26, [x0, #0xD8] /* X26 */
    str x27, [x0, #0xE0] /* X27 */
    str x28, [x0, #0xE8] /* X28 */
    str x29, [x0, #0xF0] /* FP/X29 */
    str x30, [x0, #0xF8] /* LR/X30 */
    
    /* Save stack pointer */
    mov x1, sp
    str x1, [x0, #0x100] /* SP */
    
    /* Save PC (return address) */
    str x30, [x0, #0x108] /* PC = LR */
    
    /* Set context flags */
    movz w1, #0x000F       /* Lower 16 bits */
    movk w1, #0x0004, lsl #16  /* Upper 16 bits - CONTEXT_FULL = 0x40000F */
    str w1, [x0, #0x0]     /* ContextFlags */
    
    ret

/* EOF */