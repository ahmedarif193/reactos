#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

NTSYSAPI
NTSTATUS
NTAPI
NtCancelIoFileEx(IN HANDLE FileHandle,
                 IN PIO_STATUS_BLOCK IoRequestToCancel OPTIONAL,
                 OUT PIO_STATUS_BLOCK IoStatusBlock);

NTSYSAPI
NTSTATUS
NTAPI
NtCancelSynchronousIoFile(IN HANDLE ThreadHandle,
                          IN PIO_STATUS_BLOCK IoRequestToCancel OPTIONAL,
                          OUT PIO_STATUS_BLOCK IoStatusBlock);

/*
 * @implemented
 */
BOOL
WINAPI
CancelIoEx(HANDLE hFile,
           LPOVERLAPPED lpOverlapped)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Status = NtCancelIoFileEx(hFile,
                              (PIO_STATUS_BLOCK)lpOverlapped,
                              &IoStatus);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
CancelSynchronousIo(HANDLE hThread)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Status = NtCancelSynchronousIoFile(hThread,
                                       NULL,
                                       &IoStatus);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}
