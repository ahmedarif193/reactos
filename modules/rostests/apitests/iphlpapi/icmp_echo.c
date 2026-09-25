/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Concurrent ICMP echo requests must receive their own replies.
 */

#include <apitest.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#define ECHO_WORKERS 8
#define ECHO_ROUNDS 32

typedef struct _ECHO_WORKER
{
    HANDLE Gate;
    ULONG Index;
    ULONG Completed;
    ULONG WrongCount;
    ULONG WrongReply;
    DWORD Error;
} ECHO_WORKER;

static DWORD WINAPI
EchoWorker(PVOID Parameter)
{
    ECHO_WORKER *Worker = Parameter;
    HANDLE Icmp;
    ULONG Round, Offset;
    DWORD Payload[8], Count;
    BOOL Found;
    union
    {
        ULONGLONG Alignment;
        BYTE Bytes[1024];
    } Reply;
    /* These fields are common to ICMP_ECHO_REPLY and ICMP_ECHO_REPLY32.
     * Do not interpret an embedded pointer using the other platform's ABI. */
    struct
    {
        IPAddr Address;
        DWORD Status;
        DWORD RoundTripTime;
        WORD DataSize;
        WORD Reserved;
    } Header;

    Icmp = IcmpCreateFile();
    if (Icmp == INVALID_HANDLE_VALUE)
    {
        Worker->Error = GetLastError();
        return 1;
    }
    WaitForSingleObject(Worker->Gate, INFINITE);
    for (Round = 0; Round < ECHO_ROUNDS; ++Round)
    {
        for (Offset = 0; Offset < ARRAYSIZE(Payload); ++Offset)
            Payload[Offset] = 0x49534f4c ^ (Worker->Index << 24) ^ (Round << 8) ^ Offset;
        memset(Reply.Bytes, 0xcc, sizeof(Reply.Bytes));
        Count = IcmpSendEcho(Icmp, htonl(INADDR_LOOPBACK), Payload, sizeof(Payload),
                             NULL, Reply.Bytes, sizeof(Reply.Bytes), 100);
        ++Worker->Completed;
        if (Count != 1)
        {
            ++Worker->WrongCount;
            Worker->Error = GetLastError();
        }
        if (!Count)
            continue;

        memcpy(&Header, Reply.Bytes, sizeof(Header));
        Found = FALSE;
        for (Offset = sizeof(Header); Offset <= sizeof(Reply.Bytes) - sizeof(Payload); ++Offset)
        {
            if (!memcmp(Reply.Bytes + Offset, Payload, sizeof(Payload)))
            {
                Found = TRUE;
                break;
            }
        }
        if (Header.Address != htonl(INADDR_LOOPBACK) || Header.Status != IP_SUCCESS ||
            Header.DataSize != sizeof(Payload) || !Found)
            ++Worker->WrongReply;
    }
    IcmpCloseHandle(Icmp);
    return 0;
}

START_TEST(icmp_echo)
{
    ECHO_WORKER Workers[ECHO_WORKERS] = {{0}};
    HANDLE Threads[ECHO_WORKERS], Gate;
    ULONG Index, Created = 0;
    DWORD Result;

    Gate = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Gate != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (!Gate)
        return;
    for (Index = 0; Index < ECHO_WORKERS; ++Index)
    {
        Workers[Index].Gate = Gate;
        Workers[Index].Index = Index;
        Threads[Index] = CreateThread(NULL, 0, EchoWorker, &Workers[Index], 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread %lu failed: %lu\n", Index, GetLastError());
        if (!Threads[Index])
            break;
        ++Created;
    }
    SetEvent(Gate);
    for (Index = 0; Index < Created; ++Index)
    {
        Result = WaitForSingleObject(Threads[Index], INFINITE);
        ok(Result == WAIT_OBJECT_0, "Worker %lu wait returned %lu\n", Index, Result);
        ok(Workers[Index].Completed == ECHO_ROUNDS,
           "Worker %lu completed %lu requests, error %lu\n",
           Index, Workers[Index].Completed, Workers[Index].Error);
        ok(!Workers[Index].WrongCount,
           "Worker %lu: %lu requests returned other than one reply, last error %lu\n",
           Index, Workers[Index].WrongCount, Workers[Index].Error);
        ok(!Workers[Index].WrongReply,
           "Worker %lu: %lu replies had the wrong address, status or payload\n",
           Index, Workers[Index].WrongReply);
        CloseHandle(Threads[Index]);
    }
    CloseHandle(Gate);
}
