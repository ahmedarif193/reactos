/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WiFiCx TLV generator and parser interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WDI_VERSION_MIN_SUPPORTED WDI_VERSION_1_0

#include <pshpack8.h>

typedef struct _TLV_CONTEXT
{
    ULONG_PTR   AllocationContext;
    ULONG       PeerVersion;
} TLV_CONTEXT, *PTLV_CONTEXT;
typedef const TLV_CONTEXT * PCTLV_CONTEXT;

typedef struct _EmptyMessageStructureType
{
    UINT8 _Reserved;
} EmptyMessageStructureType;

#ifdef __cplusplus
extern "C" {
#endif

    typedef _Function_class_( TlvDumperCallback ) void( __stdcall *TlvDumperCallback )(_In_ UINT_PTR Context, _In_z_ _Printf_format_string_ PCSTR Format, ...);

#ifdef __cplusplus
}
#endif

#include "TlvGenerated_.hpp"

#ifdef __cplusplus
extern "C++" inline NDIS_STATUS __stdcall Generate( _In_opt_ EmptyMessageStructureType const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
{

    return GenerateWdiTaskOpen( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
}

extern "C" {
#endif

    void __stdcall FreeGenerated( _In_opt_ UINT8* pBuffer );

    void __stdcall TraceUnknownTlvByteStream(
        _In_ ULONG PeerVersion,
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer );

    void __stdcall TraceMessageTlvByteStream(
        _In_ ULONG MessageId,
        _In_ BOOLEAN fToIhv,
        _In_ ULONG PeerVersion,
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer );

    void __stdcall DumpUnknownTlvByteStream(
        _In_ ULONG PeerVersion,
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_opt_ ULONG_PTR Context,
        _In_opt_ TlvDumperCallback pCallback );

    void __stdcall DumpMessageTlvByteStream(
        _In_ ULONG MessageId,
        _In_ BOOLEAN fToIhv,
        _In_ ULONG PeerVersion,
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_opt_ ULONG_PTR Context,
        _In_opt_ TlvDumperCallback pCallback );

#ifdef __cplusplus
}
#endif

#include <poppack.h>
