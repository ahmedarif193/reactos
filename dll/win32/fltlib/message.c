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
    UNICODE_STRING64 PortName64;
    HANDLE FileHandle;
    USHORT EaValueLength;
    SIZE_T BufferSize;
    ULONG CreateOptions;
    NTSTATUS Status;
    HRESULT hr;

    if ((lpContext == NULL) != (wSizeOfContext == 0) || (dwOptions & ~FLT_PORT_VALID_OPTIONS))
    {
        return E_INVALIDARG;
    }

    if (wSizeOfContext > MAXUSHORT - sizeof(FILTER_PORT_DATA))
    {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }

    EaValueLength = (USHORT)(sizeof(FILTER_PORT_DATA) + wSizeOfContext);
    BufferSize = sizeof(FILE_FULL_EA_INFORMATION) + FLT_PORT_EA_NAME_LENGTH + EaValueLength;
    EaBuffer = RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize);
    if (EaBuffer == NULL) return E_OUTOFMEMORY;

    EaBuffer->EaNameLength = FLT_PORT_EA_NAME_LENGTH;
    EaBuffer->EaValueLength = EaValueLength;
    RtlCopyMemory(EaBuffer->EaName, FLT_PORT_EA_NAME, FLT_PORT_EA_NAME_LENGTH + 1);

    PortData = (PFILTER_PORT_DATA)&EaBuffer->EaName[FLT_PORT_EA_NAME_LENGTH + 1];
    RtlInitUnicodeString(&PortName, lpPortName);
    PortName64.Length = PortName.Length;
    PortName64.MaximumLength = PortName.MaximumLength;
    PortName64.Buffer = (ULONGLONG)(ULONG_PTR)PortName.Buffer;
    PortData->PortName = &PortName;
    PortData->PortName64 = &PortName64;
    PortData->ContextSize = wSizeOfContext;
    if (wSizeOfContext) RtlCopyMemory(PortData + 1, lpContext, wSizeOfContext);

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

    CreateOptions = (dwOptions & FLT_PORT_VALID_OPTIONS) ? FILE_SYNCHRONOUS_IO_NONALERT : 0;
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
                          BufferSize);
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
    UNREFERENCED_PARAMETER(hPort);
    UNREFERENCED_PARAMETER(lpInBuffer);
    UNREFERENCED_PARAMETER(dwInBufferSize);
    UNREFERENCED_PARAMETER(lpOutBuffer);
    UNREFERENCED_PARAMETER(dwOutBufferSize);
    UNREFERENCED_PARAMETER(lpBytesReturned);
    return E_NOTIMPL;
}

_Must_inspect_result_
HRESULT
WINAPI
FilterGetMessage(_In_ HANDLE hPort,
                 _Out_writes_bytes_(dwMessageBufferSize) PFILTER_MESSAGE_HEADER lpMessageBuffer,
                 _In_ DWORD dwMessageBufferSize,
                 _Inout_opt_ LPOVERLAPPED lpOverlapped)
{
    UNREFERENCED_PARAMETER(hPort);
    UNREFERENCED_PARAMETER(lpMessageBuffer);
    UNREFERENCED_PARAMETER(dwMessageBufferSize);
    UNREFERENCED_PARAMETER(lpOverlapped);
    return E_NOTIMPL;
}

_Must_inspect_result_
HRESULT
WINAPI
FilterReplyMessage(_In_ HANDLE hPort,
                   _In_reads_bytes_(dwReplyBufferSize) PFILTER_REPLY_HEADER lpReplyBuffer,
                   _In_ DWORD dwReplyBufferSize)
{
    UNREFERENCED_PARAMETER(hPort);
    UNREFERENCED_PARAMETER(lpReplyBuffer);
    UNREFERENCED_PARAMETER(dwReplyBufferSize);
    return E_NOTIMPL;
}
