#pragma once

#define IFENT_SOFTWARE_LOOPBACK 24 /* This is an SNMP constant from rfc1213 */

NTSTATUS GetInterfaceIPv4Address( PIP_INTERFACE Interface,
				  ULONG Type,
				  PULONG Address );
UINT CountInterfaces(VOID);
UINT CountInterfaceAddresses( PIP_INTERFACE Interface );
UINT CopyInterfaceAddressTable( PIP_INTERFACE Interface,
                                PIPADDR_ENTRY AddrTable,
                                UINT Count );
BOOLEAN IPLocalUnicastAddressExists( PIP_ADDRESS Address );
BOOLEAN IPInterfaceHasUnicastAddress( PIP_INTERFACE Interface,
                                      PIP_ADDRESS Address );
BOOLEAN IPInterfaceHasAddress( PIP_INTERFACE Interface,
                               PIP_ADDRESS Address );
BOOLEAN IPInterfaceHasBroadcastAddress( PIP_INTERFACE Interface,
                                        PIP_ADDRESS Address );
NTSTATUS IPAddInterfaceAddress( PIP_INTERFACE Interface,
                                PIP_ADDRESS Address,
                                PIP_ADDRESS Netmask,
                                PULONG NteContext );
NTSTATUS IPRemoveInterfaceAddress( ULONG NteContext );
VOID IPRemoveInterfaceAddresses( PIP_INTERFACE Interface );
NTSTATUS GetInterfaceSpeed( PIP_INTERFACE Interface, PUINT Speed );
NTSTATUS GetInterfaceName( PIP_INTERFACE Interface, PCHAR NameBuffer,
			   UINT NameMaxLen );
VOID GetInterfaceConnectionStatus( PIP_INTERFACE Interface, PULONG OperStatus );
PIP_INTERFACE FindOnLinkInterface(PIP_ADDRESS Address);
PIP_INTERFACE GetDefaultInterface(VOID);
