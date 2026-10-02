/*
* PROJECT:         Filesystem Filter Manager library
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            dll/win32/fltlib/message.c
* PURPOSE:         Handles messaging to and from the filter manager
* PROGRAMMERS:     Ged Murphy (ged.murphy@reactos.org)
*/

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <fltuser.h>
#include <fltmgr_shared.h>

#include "fltlib.h"

_Must_inspect_result_
HRESULT
WINAPI
FilterConnectCommunicationPort(_In_ LPCWSTR lpPortName,
                               _In_ DWORD dwOptions,
                               _In_reads_bytes_opt_(wSizeOfContext) LPCVOID lpContext,
                               _In_ WORD wSizeOfContext,
                               _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                               _Outptr_ HANDLE *hPort)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_FULL_EA_INFORMATION EaBuffer;
    PFILTER_PORT_DATA PortData;
    UNICODE_STRING DeviceName;
    UNICODE_STRING PortName;
    HANDLE FileHandle;
    SIZE_T EaValueLength;
    SIZE_T BufferSize;
    ULONG CreateOptions;
    NTSTATUS Status;
    HRESULT hr;

    *hPort = INVALID_HANDLE_VALUE;

    if ((lpContext == NULL) != (wSizeOfContext == 0) || (dwOptions & ~FLT_PORT_VALID_OPTIONS))
    {
        return E_INVALIDARG;
    }

    Status = RtlInitUnicodeStringEx(&PortName, lpPortName);
    if (!NT_SUCCESS(Status) || PortName.Length == 0)
    {
        return E_INVALIDARG;
    }

    EaValueLength = FIELD_OFFSET(FILTER_PORT_DATA, PortName) + PortName.Length + wSizeOfContext;
    if (EaValueLength > MAXUSHORT)
    {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }

    BufferSize = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + FLT_PORT_EA_NAME_LENGTH + 1 + EaValueLength;
    EaBuffer = RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize);
    if (EaBuffer == NULL) return E_OUTOFMEMORY;

    EaBuffer->EaNameLength = FLT_PORT_EA_NAME_LENGTH;
    EaBuffer->EaValueLength = (USHORT)EaValueLength;
    RtlCopyMemory(EaBuffer->EaName, FLT_PORT_EA_NAME, FLT_PORT_EA_NAME_LENGTH + 1);

    PortData = (PFILTER_PORT_DATA)&EaBuffer->EaName[FLT_PORT_EA_NAME_LENGTH + 1];
    PortData->PortNameLength = PortName.Length;
    PortData->ContextSize = wSizeOfContext;
    RtlCopyMemory(PortData->PortName, PortName.Buffer, PortName.Length);
    if (wSizeOfContext) RtlCopyMemory((PUCHAR)PortData->PortName + PortName.Length, lpContext, wSizeOfContext);

    /* Initialize the object attributes */
    RtlInitUnicodeString(&DeviceName, L"\\Global??\\FltMgrMsg");
    InitializeObjectAttributes(&ObjectAttributes,
                               &DeviceName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    /* Check if we were passed any security attributes */
    if (lpSecurityAttributes)
    {
        /* Add these manually and update the flags if we were asked to make it inheritable */
        ObjectAttributes.SecurityDescriptor = lpSecurityAttributes->lpSecurityDescriptor;
        if (lpSecurityAttributes->bInheritHandle)
        {
            ObjectAttributes.Attributes |= OBJ_INHERIT;
        }
    }

    CreateOptions = (dwOptions & FLT_PORT_FLAG_SYNC_HANDLE) ? FILE_SYNCHRONOUS_IO_NONALERT : 0;
    Status = NtCreateFile(&FileHandle,
                          SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          0,
                          0,
                          0,
                          FILE_OPEN,
                          CreateOptions,
                          EaBuffer,
                          (ULONG)BufferSize);
    if (NT_SUCCESS(Status))
    {
        *hPort = FileHandle;
        hr = S_OK;
    }
    else
    {
        hr = NtStatusToHResult(Status);
    }

    /* Cleanup and return */
    RtlFreeHeap(GetProcessHeap(), 0, EaBuffer);
    return hr;
}

static
NTSTATUS
PortIoctl(_In_ HANDLE hPort,
          _In_ ULONG IoControlCode,
          _In_reads_bytes_opt_(InSize) PVOID InBuffer,
          _In_ ULONG InSize,
          _Out_writes_bytes_opt_(OutSize) PVOID OutBuffer,
          _In_ ULONG OutSize,
          _Out_opt_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = NtDeviceIoControlFile(hPort,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IoControlCode,
                                   InBuffer,
                                   InSize,
                                   OutBuffer,
                                   OutSize);
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(hPort, FALSE, NULL);
        if (NT_SUCCESS(Status))
        {
            Status = IoStatusBlock.Status;
        }
    }

    if (Information != NULL)
    {
        *Information = NT_ERROR(Status) ? 0 : IoStatusBlock.Information;
    }
    return Status;
}

_Must_inspect_result_
HRESULT
WINAPI
FilterSendMessage(_In_ HANDLE hPort,
                  _In_reads_bytes_(dwInBufferSize) LPVOID lpInBuffer,
                  _In_ DWORD dwInBufferSize,
                  _Out_writes_bytes_to_opt_(dwOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
                  _In_ DWORD dwOutBufferSize,
                  _Out_ LPDWORD lpBytesReturned)
{
    ULONG_PTR Information;
    NTSTATUS Status;

    Status = PortIoctl(hPort,
                       IOCTL_FILTER_SEND_MESSAGE,
                       lpInBuffer,
                       dwInBufferSize,
                       lpOutBuffer,
                       dwOutBufferSize,
                       &Information);
    *lpBytesReturned = (DWORD)Information;
    return NtStatusToHResult(Status);
}

_Must_inspect_result_
HRESULT
WINAPI
FilterGetMessage(_In_ HANDLE hPort,
                 _Out_writes_bytes_(dwMessageBufferSize) PFILTER_MESSAGE_HEADER lpMessageBuffer,
                 _In_ DWORD dwMessageBufferSize,
                 _Inout_opt_ LPOVERLAPPED lpOverlapped)
{
    NTSTATUS Status;

    if (lpOverlapped == NULL)
    {
        Status = PortIoctl(hPort,
                           IOCTL_FILTER_GET_MESSAGE,
                           NULL,
                           0,
                           lpMessageBuffer,
                           dwMessageBufferSize,
                           NULL);
        return NtStatusToHResult(Status);
    }

    lpOverlapped->Internal = STATUS_PENDING;
    Status = NtDeviceIoControlFile(hPort,
                                   lpOverlapped->hEvent,
                                   NULL,
                                   ((ULONG_PTR)lpOverlapped->hEvent & 1) ? NULL : lpOverlapped,
                                   (PIO_STATUS_BLOCK)lpOverlapped,
                                   IOCTL_FILTER_GET_MESSAGE,
                                   NULL,
                                   0,
                                   lpMessageBuffer,
                                   dwMessageBufferSize);
    if (Status == STATUS_PENDING)
    {
        return HRESULT_FROM_WIN32(ERROR_IO_PENDING);
    }
    return NtStatusToHResult(Status);
}

_Must_inspect_result_
HRESULT
WINAPI
FilterReplyMessage(_In_ HANDLE hPort,
                   _In_reads_bytes_(dwReplyBufferSize) PFILTER_REPLY_HEADER lpReplyBuffer,
                   _In_ DWORD dwReplyBufferSize)
{
    NTSTATUS Status;

    Status = PortIoctl(hPort,
                       IOCTL_FILTER_REPLY_MESSAGE,
                       lpReplyBuffer,
                       dwReplyBufferSize,
                       NULL,
                       0,
                       NULL);
    return NtStatusToHResult(Status);
}
