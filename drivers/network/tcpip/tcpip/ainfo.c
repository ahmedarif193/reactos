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
      case AO_OPTION_MCASTIF:
      {
         ULONG Selector;
         PIP_INTERFACE Interface;
         if (BufferSize < sizeof(Selector)) return TDI_INVALID_PARAMETER;
         RtlCopyMemory(&Selector, Buffer, sizeof(Selector));
         if (Selector)
         {
             Interface = GetMulticastInterface(Selector);
             if (!Interface) return TDI_INVALID_PARAMETER;
             IPDereferenceInterface(Interface);
         }
         LockObject(AddrFile);
         AddrFile->MulticastInterface = Selector;
         UnlockObject(AddrFile);
         return TDI_SUCCESS;
      }

      case AO_OPTION_MCASTTTL:
         if (BufferSize < sizeof(UINT) || *(PUINT)Buffer > 255)
             return TDI_INVALID_PARAMETER;
         LockObject(AddrFile);
         AddrFile->MulticastTTL = (UCHAR)*(PUINT)Buffer;
         UnlockObject(AddrFile);
         return TDI_SUCCESS;

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
    UNIMPLEMENTED;

    return TDI_INVALID_REQUEST;
}
