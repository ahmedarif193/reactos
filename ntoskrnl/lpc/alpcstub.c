/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ALPC syscall fallback for builds using the classic LPC kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

#define ALPC_DISABLED() return STATUS_NOT_IMPLEMENTED

NTSTATUS NTAPI
NtAlpcCreatePort(PHANDLE PortHandle,
                 POBJECT_ATTRIBUTES ObjectAttributes,
                 PALPC_PORT_ATTRIBUTES PortAttributes)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(PortAttributes);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcDisconnectPort(HANDLE PortHandle, ULONG Flags)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcQueryInformation(HANDLE PortHandle,
                       ALPC_PORT_INFORMATION_CLASS PortInformationClass,
                       PVOID PortInformation,
                       ULONG Length,
                       PULONG ReturnLength)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(PortInformationClass);
    UNREFERENCED_PARAMETER(PortInformation);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(ReturnLength);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcSetInformation(HANDLE PortHandle,
                     ALPC_PORT_INFORMATION_CLASS PortInformationClass,
                     PVOID PortInformation,
                     ULONG Length)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(PortInformationClass);
    UNREFERENCED_PARAMETER(PortInformation);
    UNREFERENCED_PARAMETER(Length);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcCreatePortSection(HANDLE PortHandle,
                        ULONG Flags,
                        HANDLE SectionHandle,
                        SIZE_T SectionSize,
                        PALPC_HANDLE AlpcSectionHandle,
                        PSIZE_T ActualSectionSize)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(SectionHandle);
    UNREFERENCED_PARAMETER(SectionSize);
    UNREFERENCED_PARAMETER(AlpcSectionHandle);
    UNREFERENCED_PARAMETER(ActualSectionSize);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcDeletePortSection(HANDLE PortHandle,
                        ULONG Flags,
                        ALPC_HANDLE SectionHandle)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(SectionHandle);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcCreateResourceReserve(HANDLE PortHandle,
                            ULONG Flags,
                            SIZE_T MessageSize,
                            PULONG ResourceId)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(MessageSize);
    UNREFERENCED_PARAMETER(ResourceId);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcDeleteResourceReserve(HANDLE PortHandle,
                            ULONG Flags,
                            ULONG ResourceId)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ResourceId);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcCreateSectionView(HANDLE PortHandle,
                        ULONG Flags,
                        PALPC_DATA_VIEW_ATTR ViewAttributes)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ViewAttributes);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcDeleteSectionView(HANDLE PortHandle, ULONG Flags, PVOID ViewBase)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ViewBase);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcCreateSecurityContext(HANDLE PortHandle,
                            ULONG Flags,
                            PALPC_SECURITY_ATTR SecurityAttribute)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(SecurityAttribute);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcDeleteSecurityContext(HANDLE PortHandle,
                            ULONG Flags,
                            ALPC_HANDLE ContextHandle)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ContextHandle);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcRevokeSecurityContext(HANDLE PortHandle,
                            ULONG Flags,
                            ALPC_HANDLE ContextHandle)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ContextHandle);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcQueryInformationMessage(HANDLE PortHandle,
                              PPORT_MESSAGE PortMessage,
                              ALPC_MESSAGE_INFORMATION_CLASS MessageInformationClass,
                              PVOID MessageInformation,
                              ULONG Length,
                              PULONG ReturnLength)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(PortMessage);
    UNREFERENCED_PARAMETER(MessageInformationClass);
    UNREFERENCED_PARAMETER(MessageInformation);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(ReturnLength);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcConnectPort(PHANDLE PortHandle,
                  PUNICODE_STRING PortName,
                  POBJECT_ATTRIBUTES ObjectAttributes,
                  PALPC_PORT_ATTRIBUTES PortAttributes,
                  ULONG Flags,
                  PSID RequiredServerSid,
                  PPORT_MESSAGE ConnectionMessage,
                  PSIZE_T BufferLength,
                  PALPC_MESSAGE_ATTRIBUTES OutMessageAttributes,
                  PALPC_MESSAGE_ATTRIBUTES InMessageAttributes,
                  PLARGE_INTEGER Timeout)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(PortName);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(PortAttributes);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(RequiredServerSid);
    UNREFERENCED_PARAMETER(ConnectionMessage);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(OutMessageAttributes);
    UNREFERENCED_PARAMETER(InMessageAttributes);
    UNREFERENCED_PARAMETER(Timeout);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcConnectPortEx(PHANDLE PortHandle,
                    POBJECT_ATTRIBUTES ConnectionPortObjectAttributes,
                    POBJECT_ATTRIBUTES ClientPortObjectAttributes,
                    PALPC_PORT_ATTRIBUTES PortAttributes,
                    ULONG Flags,
                    PSECURITY_DESCRIPTOR ServerSecurityRequirements,
                    PPORT_MESSAGE ConnectionMessage,
                    PSIZE_T BufferLength,
                    PALPC_MESSAGE_ATTRIBUTES OutMessageAttributes,
                    PALPC_MESSAGE_ATTRIBUTES InMessageAttributes,
                    PLARGE_INTEGER Timeout)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(ConnectionPortObjectAttributes);
    UNREFERENCED_PARAMETER(ClientPortObjectAttributes);
    UNREFERENCED_PARAMETER(PortAttributes);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ServerSecurityRequirements);
    UNREFERENCED_PARAMETER(ConnectionMessage);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(OutMessageAttributes);
    UNREFERENCED_PARAMETER(InMessageAttributes);
    UNREFERENCED_PARAMETER(Timeout);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcAcceptConnectPort(PHANDLE PortHandle,
                        HANDLE ConnectionPortHandle,
                        ULONG Flags,
                        POBJECT_ATTRIBUTES ObjectAttributes,
                        PALPC_PORT_ATTRIBUTES PortAttributes,
                        PVOID PortContext,
                        PPORT_MESSAGE ConnectionRequest,
                        PALPC_MESSAGE_ATTRIBUTES ConnectionMessageAttributes,
                        BOOLEAN AcceptConnection)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(ConnectionPortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(PortAttributes);
    UNREFERENCED_PARAMETER(PortContext);
    UNREFERENCED_PARAMETER(ConnectionRequest);
    UNREFERENCED_PARAMETER(ConnectionMessageAttributes);
    UNREFERENCED_PARAMETER(AcceptConnection);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcSendWaitReceivePort(HANDLE PortHandle,
                          ULONG Flags,
                          PPORT_MESSAGE SendMessage,
                          PALPC_MESSAGE_ATTRIBUTES SendMessageAttributes,
                          PPORT_MESSAGE ReceiveMessage,
                          PSIZE_T BufferLength,
                          PALPC_MESSAGE_ATTRIBUTES ReceiveMessageAttributes,
                          PLARGE_INTEGER Timeout)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(SendMessage);
    UNREFERENCED_PARAMETER(SendMessageAttributes);
    UNREFERENCED_PARAMETER(ReceiveMessage);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(ReceiveMessageAttributes);
    UNREFERENCED_PARAMETER(Timeout);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcCancelMessage(HANDLE PortHandle,
                    ULONG Flags,
                    PALPC_CONTEXT_ATTR MessageContext)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(MessageContext);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcImpersonateClientOfPort(HANDLE PortHandle,
                              PPORT_MESSAGE Message,
                              PVOID Flags)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Message);
    UNREFERENCED_PARAMETER(Flags);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcImpersonateClientContainerOfPort(HANDLE PortHandle,
                                       PPORT_MESSAGE Message,
                                       ULONG Flags)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Message);
    UNREFERENCED_PARAMETER(Flags);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcOpenSenderProcess(PHANDLE ProcessHandle,
                        HANDLE PortHandle,
                        PPORT_MESSAGE PortMessage,
                        ULONG Flags,
                        ACCESS_MASK DesiredAccess,
                        POBJECT_ATTRIBUTES ObjectAttributes)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(PortMessage);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
NtAlpcOpenSenderThread(PHANDLE ThreadHandle,
                       HANDLE PortHandle,
                       PPORT_MESSAGE PortMessage,
                       ULONG Flags,
                       ACCESS_MASK DesiredAccess,
                       POBJECT_ATTRIBUTES ObjectAttributes)
{
    UNREFERENCED_PARAMETER(ThreadHandle);
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(PortMessage);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    ALPC_DISABLED();
}

NTSTATUS NTAPI
AlpcCreateSecurityContext(HANDLE PortHandle,
                          PETHREAD Thread,
                          ULONG Flags,
                          PALPC_SECURITY_ATTR SecurityAttribute)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(SecurityAttribute);
    ALPC_DISABLED();
}
