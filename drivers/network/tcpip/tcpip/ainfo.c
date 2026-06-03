/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/ainfo.c
 * PURPOSE:     Per-socket information.
 * PROGRAMMER:  Cameron Gutman
 */

#include "precomp.h"

TDI_STATUS SetAddressFileInfo(TDIObjectID *ID,
                              PADDRESS_FILE AddrFile,
                              PVOID Buffer,
                              UINT BufferSize)
{
    switch (ID->toi_id)
    {
      case AO_OPTION_TTL:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->TTL = *((PUCHAR)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_IP_DONTFRAGMENT:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->DF = *((PUINT)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_BROADCAST:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->BCast = *((PUINT)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_IP_HDRINCL:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->HeaderIncl = *((PUINT)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      default:
         DbgPrint("Unimplemented option %x\n", ID->toi_id);

         return TDI_INVALID_REQUEST;
    }
}

TDI_STATUS GetAddressFileInfo(TDIObjectID *ID,
                              PADDRESS_FILE AddrFile,
                              PVOID Buffer,
                              PUINT BufferSize)
{
    UINT Value;

    LockObject(AddrFile);

    switch (ID->toi_id)
    {
      case AO_OPTION_TTL:
         Value = AddrFile->TTL;
         break;

      case AO_OPTION_IP_DONTFRAGMENT:
         Value = AddrFile->DF;
         break;

      case AO_OPTION_BROADCAST:
         Value = AddrFile->BCast;
         break;

      case AO_OPTION_IP_HDRINCL:
         Value = AddrFile->HeaderIncl;
         break;

      default:
         UnlockObject(AddrFile);
         DbgPrint("Unimplemented option %x\n", ID->toi_id);

         return TDI_INVALID_REQUEST;
    }

    UnlockObject(AddrFile);

    return InfoCopyOut((PCHAR)&Value, sizeof(Value), Buffer, BufferSize);
}
