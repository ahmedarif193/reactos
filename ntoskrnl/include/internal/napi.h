/*
 * FILE:            ntoskrnl/include/internal/napi.h
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PURPOSE:         System Call Table for Native API
 * PROGRAMMER:      Timo Kreuzer
 */

NTSTATUS
NTAPI
NtQuerySystemInformationEx(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_to_opt_(SystemInformationLength, *ReturnLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength);

NTSTATUS
NTAPI
NtCancelIoFileEx(
    _In_ HANDLE FileHandle,
    _In_opt_ PIO_STATUS_BLOCK IoRequestToCancel,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock);

/* Generated from ntoskrnl/sysfuncs.lst by gen_syscalls */
#include <internal/syscall_table.h>

#define SVC_(name, argcount) (ULONG_PTR)Nt##name,
ULONG_PTR MainSSDT[] = {
#include <internal/syscalls_body.h>
};
#undef SVC_

#define SVC_(name, argcount) argcount * sizeof(void *),
UCHAR MainSSPT[] = {
#include <internal/syscalls_body.h>
};
#undef SVC_

#define MIN_SYSCALL_NUMBER    0
#define MAX_SYSCALL_NUMBER    (NUMBER_OF_SYSCALLS - 1)
ULONG MainNumberOfSysCalls = NUMBER_OF_SYSCALLS;
