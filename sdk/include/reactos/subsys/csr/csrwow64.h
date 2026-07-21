/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Client/Server Runtime SubSystem
 * FILE:            include/reactos/subsys/csr/csrwow64.h
 * PURPOSE:         Fixed-width CSR structures used by WoW64
 */

#ifndef _CSRWOW64_H
#define _CSRWOW64_H

#pragma once

typedef struct _CSR_PORT_MESSAGE32
{
    USHORT DataLength;
    USHORT TotalLength;
    USHORT Type;
    USHORT DataInfoOffset;
    CLIENT_ID32 ClientId;
    ULONG MessageId;
    ULONG ClientViewSize;
} CSR_PORT_MESSAGE32, *PCSR_PORT_MESSAGE32;

typedef struct _CSR_API_CONNECTINFO32
{
    ULONG ObjectDirectory;
    ULONG SharedSectionBase;
    ULONG SharedStaticServerData;
    ULONG SharedSectionHeap;
    ULONG DebugFlags;
    ULONG SizeOfPebData;
    ULONG SizeOfTebData;
    ULONG NumberOfServerDllNames;
    ULONG ServerProcessId;
} CSR_API_CONNECTINFO32, *PCSR_API_CONNECTINFO32;

typedef struct _CSR_CLIENT_CONNECT32
{
    ULONG ServerId;
    ULONG ConnectionInfo;
    ULONG ConnectionInfoSize;
} CSR_CLIENT_CONNECT32, *PCSR_CLIENT_CONNECT32;

typedef struct _CSR_CAPTURE_BUFFER32
{
    ULONG Size;
    ULONG PreviousCaptureBuffer;
    ULONG PointerCount;
    ULONG BufferEnd;
    ULONG PointerOffsetsArray[ANYSIZE_ARRAY];
} CSR_CAPTURE_BUFFER32, *PCSR_CAPTURE_BUFFER32;

typedef struct _CSR_API_MESSAGE32
{
    CSR_PORT_MESSAGE32 Header;
    ULONG CsrCaptureData;
    ULONG ApiNumber;
    NTSTATUS Status;
    ULONG Reserved;
    union
    {
        CSR_CLIENT_CONNECT32 CsrClientConnect;
        ULONG ApiMessageData[39];
    } Data;
} CSR_API_MESSAGE32, *PCSR_API_MESSAGE32;

#ifdef _WIN64

typedef struct _CSR_PORT_MESSAGE64
{
    USHORT DataLength;
    USHORT TotalLength;
    USHORT Type;
    USHORT DataInfoOffset;
    ULONGLONG UniqueProcess;
    ULONGLONG UniqueThread;
    ULONG MessageId;
    ULONG Padding;
    ULONGLONG ClientViewSize;
} CSR_PORT_MESSAGE64, *PCSR_PORT_MESSAGE64;

typedef struct _CSR_CLIENT_CONNECT64
{
    ULONG ServerId;
    ULONG Padding1;
    ULONGLONG ConnectionInfo;
    ULONG ConnectionInfoSize;
    ULONG Padding2;
} CSR_CLIENT_CONNECT64, *PCSR_CLIENT_CONNECT64;

typedef struct _CSR_API_MESSAGE64
{
    CSR_PORT_MESSAGE64 Header;
    ULONGLONG CsrCaptureData;
    ULONG ApiNumber;
    NTSTATUS Status;
    ULONG Reserved;
    ULONG Padding;
    union
    {
        CSR_CLIENT_CONNECT64 CsrClientConnect;
        ULONGLONG ApiMessageData[39];
    } Data;
} CSR_API_MESSAGE64, *PCSR_API_MESSAGE64;

#endif /* _WIN64 */

C_ASSERT(sizeof(CSR_PORT_MESSAGE32) == 0x18);
C_ASSERT(sizeof(CSR_API_CONNECTINFO32) == 0x24);
C_ASSERT(sizeof(CSR_CLIENT_CONNECT32) == 0x0c);
C_ASSERT(FIELD_OFFSET(CSR_CAPTURE_BUFFER32, PointerOffsetsArray) == 0x10);
C_ASSERT(FIELD_OFFSET(CSR_API_MESSAGE32, Data) == 0x28);

#ifdef _WIN64
C_ASSERT(sizeof(CSR_PORT_MESSAGE64) == 0x28);
C_ASSERT(sizeof(CSR_CLIENT_CONNECT64) == 0x18);
C_ASSERT(FIELD_OFFSET(CSR_API_MESSAGE64, Data) == 0x40);
#endif

#endif /* _CSRWOW64_H */
