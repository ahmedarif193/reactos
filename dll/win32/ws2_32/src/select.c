/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS WinSock 2 API
 * FILE:        dll/win32/ws2_32/src/select.c
 * PURPOSE:     Socket Select Support
 * PROGRAMMER:  Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES ******************************************************************/

#include <ws2_32.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
INT
WSPAPI
__WSAFDIsSet(SOCKET s,
             LPFD_SET set)
{
    INT i = set->fd_count;
    INT Return = FALSE;

    /* Loop until a match is found */
    while (i--) if (set->fd_array[i] == s) Return = TRUE;

    /* Return */
    return Return;
}

/*
 * @implemented
 */
INT
WSAAPI
select(IN INT s,
       IN OUT LPFD_SET readfds,
       IN OUT LPFD_SET writefds,
       IN OUT LPFD_SET exceptfds,
       IN CONST struct timeval *timeout)
{
    PWSSOCKET Socket;
    INT Status;
    INT ErrorCode;
    SOCKET Handle;
    LPWSPSELECT WSPSelect;

    DPRINT("select: %lx %p %p %p %p\n", s, readfds, writefds, exceptfds, timeout);

    /* Check for WSAStartup */
    ErrorCode = WsQuickProlog();

    if (ErrorCode != ERROR_SUCCESS)
    {
        SetLastError(ErrorCode);
        return SOCKET_ERROR;
    }

    /* Use the first Socket from the first valid set */
    if (readfds && readfds->fd_count)
    {
        Handle = readfds->fd_array[0];
    }
    else if (writefds && writefds->fd_count)
    {
        Handle = writefds->fd_array[0];
    }
    else if (exceptfds && exceptfds->fd_count)
    {
        Handle = exceptfds->fd_array[0];
    }
    else
    {
        /* Invalid handles */
        SetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    /* Get the Socket Context */
    Socket = WsSockGetSocket(Handle);

    if (!Socket)
    {
        /* No Socket Context Found */
        SetLastError(WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Get the select procedure */
    WSPSelect = Socket->Provider->Service.lpWSPSelect;

    /* Make the call */
    Status = WSPSelect(s, readfds, writefds, exceptfds, (struct timeval *)timeout,
                       &ErrorCode);

    /* Deference the Socket Context */
    WsSockDereference(Socket);

    /* Return Provider Value */
    if (Status != SOCKET_ERROR)
        return Status;

    /* If everything seemed fine, then the WSP call failed itself */
    if (ErrorCode == NO_ERROR)
        ErrorCode = WSASYSCALLFAILURE;

    /* Return with an error */
    SetLastError(ErrorCode);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WPUFDIsSet(IN SOCKET s,
           IN LPFD_SET set)
{
    UNIMPLEMENTED;
    return (SOCKET)0;
}

/*
 * @implemented
 */
INT
WSAAPI
WSAAsyncSelect(IN SOCKET s,
               IN HWND hWnd,
               IN UINT wMsg,
               IN LONG lEvent)
{
    PWSSOCKET Socket;
    INT Status;
    INT ErrorCode;
    DPRINT("WSAAsyncSelect: %lx, %lx, %lx, %lx\n", s, hWnd, wMsg, lEvent);

    /* Check for WSAStartup */
    if ((ErrorCode = WsQuickProlog()) == ERROR_SUCCESS)
    {
        /* Get the Socket Context */
        if ((Socket = WsSockGetSocket(s)))
        {
            /* Make the call */
            Status = Socket->Provider->Service.lpWSPAsyncSelect(s,
                                                                hWnd,
                                                                wMsg,
                                                                lEvent,
                                                                &ErrorCode);
            /* Deference the Socket Context */
            WsSockDereference(Socket);

            /* Return Provider Value */
            if (Status == ERROR_SUCCESS) return Status;

            /* If everything seemed fine, then the WSP call failed itself */
            if (ErrorCode == NO_ERROR) ErrorCode = WSASYSCALLFAILURE;
        }
        else
        {
            /* No Socket Context Found */
            ErrorCode = WSAENOTSOCK;
        }
    }

    /* Return with an Error */
    SetLastError(ErrorCode);
    return SOCKET_ERROR;
}

/*
 * @implemented
 */
INT
WSAAPI
WSAEventSelect(IN SOCKET s,
               IN WSAEVENT hEventObject,
               IN LONG lNetworkEvents)
{
    PWSSOCKET Socket;
    INT Status;
    INT ErrorCode;

    /* Check for WSAStartup */
    if ((ErrorCode = WsQuickProlog()) == ERROR_SUCCESS)
    {
        /* Get the Socket Context */
        if ((Socket = WsSockGetSocket(s)))
        {
            /* Make the call */
            Status = Socket->Provider->Service.lpWSPEventSelect(s,
                                                        hEventObject,
                                                        lNetworkEvents,
                                                        &ErrorCode);
            /* Deference the Socket Context */
            WsSockDereference(Socket);

            /* Return Provider Value */
            if (Status == ERROR_SUCCESS) return Status;
        }
        else
        {
            /* No Socket Context Found */
            ErrorCode = WSAENOTSOCK;
        }
    }

    /* Return with an Error */
    SetLastError(ErrorCode);
    return SOCKET_ERROR;
}

INT
WSAAPI
WSAPoll(IN OUT LPWSAPOLLFD fdArray,
        IN ULONG fds,
        IN INT timeout)
{
    struct timeval TimeValue;
    struct timeval *TimeOut;
    LPFD_SET ReadSet = NULL;
    LPFD_SET WriteSet = NULL;
    LPFD_SET ExceptSet = NULL;
    SIZE_T SetSize;
    ULONG Index;
    ULONG Valid = 0;
    INT Status;
    INT Result = 0;
    INT ErrorCode;

    ErrorCode = WsQuickProlog();
    if (ErrorCode != ERROR_SUCCESS)
    {
        SetLastError(ErrorCode);
        return SOCKET_ERROR;
    }

    if (fdArray == NULL || fds == 0)
    {
        SetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    SetSize = FIELD_OFFSET(FD_SET, fd_array) + (SIZE_T)fds * sizeof(SOCKET);
    ReadSet = HeapAlloc(WsSockHeap, HEAP_ZERO_MEMORY, SetSize);
    WriteSet = HeapAlloc(WsSockHeap, HEAP_ZERO_MEMORY, SetSize);
    ExceptSet = HeapAlloc(WsSockHeap, HEAP_ZERO_MEMORY, SetSize);

    if (ReadSet == NULL || WriteSet == NULL || ExceptSet == NULL)
    {
        if (ReadSet) HeapFree(WsSockHeap, 0, ReadSet);
        if (WriteSet) HeapFree(WsSockHeap, 0, WriteSet);
        if (ExceptSet) HeapFree(WsSockHeap, 0, ExceptSet);
        SetLastError(WSAENOBUFS);
        return SOCKET_ERROR;
    }

    for (Index = 0; Index < fds; Index++)
    {
        fdArray[Index].revents = 0;

        if (fdArray[Index].fd == INVALID_SOCKET)
            continue;

        Valid++;

        if (fdArray[Index].events & (POLLRDNORM | POLLRDBAND))
            ReadSet->fd_array[ReadSet->fd_count++] = fdArray[Index].fd;

        if (fdArray[Index].events & POLLWRNORM)
            WriteSet->fd_array[WriteSet->fd_count++] = fdArray[Index].fd;

        ExceptSet->fd_array[ExceptSet->fd_count++] = fdArray[Index].fd;
    }

    if (Valid == 0)
    {
        HeapFree(WsSockHeap, 0, ReadSet);
        HeapFree(WsSockHeap, 0, WriteSet);
        HeapFree(WsSockHeap, 0, ExceptSet);
        SetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    if (timeout < 0)
    {
        TimeOut = NULL;
    }
    else
    {
        TimeValue.tv_sec = timeout / 1000;
        TimeValue.tv_usec = (timeout % 1000) * 1000;
        TimeOut = &TimeValue;
    }

    Status = select(0,
                    ReadSet->fd_count ? ReadSet : NULL,
                    WriteSet->fd_count ? WriteSet : NULL,
                    ExceptSet->fd_count ? ExceptSet : NULL,
                    TimeOut);

    if (Status == SOCKET_ERROR)
    {
        HeapFree(WsSockHeap, 0, ReadSet);
        HeapFree(WsSockHeap, 0, WriteSet);
        HeapFree(WsSockHeap, 0, ExceptSet);
        return SOCKET_ERROR;
    }

    for (Index = 0; Index < fds; Index++)
    {
        SHORT Events = 0;

        if (fdArray[Index].fd == INVALID_SOCKET)
            continue;

        if (ReadSet->fd_count && __WSAFDIsSet(fdArray[Index].fd, ReadSet))
            Events |= (fdArray[Index].events & (POLLRDNORM | POLLRDBAND));

        if (WriteSet->fd_count && __WSAFDIsSet(fdArray[Index].fd, WriteSet))
            Events |= (fdArray[Index].events & POLLWRNORM);

        if (ExceptSet->fd_count && __WSAFDIsSet(fdArray[Index].fd, ExceptSet))
            Events |= POLLERR;

        fdArray[Index].revents = Events;

        if (Events != 0)
            Result++;
    }

    HeapFree(WsSockHeap, 0, ReadSet);
    HeapFree(WsSockHeap, 0, WriteSet);
    HeapFree(WsSockHeap, 0, ExceptSet);

    return Result;
}
