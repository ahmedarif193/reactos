/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx 2.3 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NETADAPTERPACKET_2_3_H_
#define _NETADAPTERPACKET_2_3_H_

#ifndef WDF_EXTERN_C
  #ifdef __cplusplus
    #define WDF_EXTERN_C       extern "C"
    #define WDF_EXTERN_C_START extern "C" {
    #define WDF_EXTERN_C_END   }
  #else
    #define WDF_EXTERN_C
    #define WDF_EXTERN_C_START
    #define WDF_EXTERN_C_END
  #endif
#endif

WDF_EXTERN_C_START

typedef struct _NET_EXTENSION_QUERY
{

    ULONG Size;

    PCWSTR Name;
    ULONG Version;

    NET_EXTENSION_TYPE
        Type;

} NET_EXTENSION_QUERY;

inline
void
NET_EXTENSION_QUERY_INIT(
    _Out_ NET_EXTENSION_QUERY * Extension,
    _In_ PCWSTR Name,
    _In_ ULONG Version,
    _In_ NET_EXTENSION_TYPE Type
)
{
    RtlZeroMemory(Extension, sizeof(NET_EXTENSION_QUERY));
    Extension->Size = sizeof(NET_EXTENSION_QUERY);

    Extension->Name = Name;
    Extension->Version = Version;
    Extension->Type = Type;
}

WDF_EXTERN_C_END

#endif
