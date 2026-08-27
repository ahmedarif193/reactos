/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         End-to-end wait-completion packet tests
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTSYSAPI
NTSTATUS
NTAPI
ZwCreateIoCompletion(
    _Out_ PHANDLE IoCompletionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG NumberOfConcurrentThreads);

NTSYSAPI
NTSTATUS
NTAPI
ZwRemoveIoCompletion(
    _In_ HANDLE IoCompletionHandle,
    _Out_ PVOID *CompletionKey,
    _Out_ PVOID *CompletionContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER Timeout);

NTSYSAPI
NTSTATUS
NTAPI
ZwCreateWaitCompletionPacket(
    _Out_ PHANDLE WaitCompletionPacketHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes);

NTSYSAPI
NTSTATUS
NTAPI
ZwAssociateWaitCompletionPacket(
    _In_ HANDLE WaitCompletionPacketHandle,
    _In_ HANDLE IoCompletionHandle,
    _In_ HANDLE TargetObjectHandle,
    _In_opt_ PVOID KeyContext,
    _In_opt_ PVOID ApcContext,
    _In_ NTSTATUS IoStatus,
    _In_ ULONG_PTR IoStatusInformation,
    _Out_opt_ PBOOLEAN AlreadySignaled);

NTSYSAPI
NTSTATUS
NTAPI
ZwCancelWaitCompletionPacket(
    _In_ HANDLE WaitCompletionPacketHandle,
    _In_ BOOLEAN RemoveSignaledPacket);

START_TEST(IoWaitCompletionPacket)
{
    static const PVOID ExpectedKey = (PVOID)(ULONG_PTR)0x1111;
    static const PVOID ExpectedContext = (PVOID)(ULONG_PTR)0x2222;
    static const NTSTATUS ExpectedStatus = STATUS_GRAPHICS_PRESENT_OCCLUDED;
    static const ULONG_PTR ExpectedInformation = 0x3333;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Timeout;
    HANDLE CompletionHandle = NULL;
    HANDLE EventHandle = NULL;
    HANDLE PacketHandle = NULL;
    PVOID CompletionKey = NULL;
    PVOID CompletionContext = NULL;
    BOOLEAN AlreadySignaled = TRUE;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateIoCompletion(&CompletionHandle,
                                  IO_COMPLETION_ALL_ACCESS,
                                  &ObjectAttributes,
                                  1);
    trace("ZwCreateIoCompletion returned 0x%08lx, handle %p\n", Status, CompletionHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ZwCreateEvent(&EventHandle,
                           EVENT_ALL_ACCESS,
                           &ObjectAttributes,
                           NotificationEvent,
                           FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = ZwCreateWaitCompletionPacket(&PacketHandle,
                                          GENERIC_ALL,
                                          &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = ZwAssociateWaitCompletionPacket(PacketHandle,
                                             CompletionHandle,
                                             EventHandle,
                                             ExpectedKey,
                                             ExpectedContext,
                                             ExpectedStatus,
                                             ExpectedInformation,
                                             &AlreadySignaled);
    trace("ZwAssociateWaitCompletionPacket returned 0x%08lx, already-signaled %u\n", Status, AlreadySignaled);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(AlreadySignaled == FALSE,
       "unsignaled event reported AlreadySignaled=%u\n",
       AlreadySignaled);

    Status = ZwSetEvent(EventHandle, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Timeout.QuadPart = -10LL * 1000 * 1000;
    RtlZeroMemory(&IoStatusBlock, sizeof(IoStatusBlock));
    Status = ZwRemoveIoCompletion(CompletionHandle,
                                  &CompletionKey,
                                  &CompletionContext,
                                  &IoStatusBlock,
                                  &Timeout);
    trace("ZwRemoveIoCompletion returned 0x%08lx, key %p, context %p, packet status 0x%08lx, information %Ix\n",
          Status, CompletionKey, CompletionContext, IoStatusBlock.Status, IoStatusBlock.Information);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(CompletionKey == ExpectedKey,
           "CompletionKey=%p expected %p\n",
           CompletionKey,
           ExpectedKey);
        ok(CompletionContext == ExpectedContext,
           "CompletionContext=%p expected %p\n",
           CompletionContext,
           ExpectedContext);
        ok_eq_hex(IoStatusBlock.Status, ExpectedStatus);
        ok(IoStatusBlock.Information == ExpectedInformation,
           "Information=%Ix expected %Ix\n",
           IoStatusBlock.Information,
           ExpectedInformation);
    }

    Status = ZwResetEvent(EventHandle, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ZwAssociateWaitCompletionPacket(PacketHandle,
                                             CompletionHandle,
                                             EventHandle,
                                             ExpectedKey,
                                             ExpectedContext,
                                             STATUS_SUCCESS,
                                             0,
                                             &AlreadySignaled);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCancelWaitCompletionPacket(PacketHandle, FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    Timeout.QuadPart = 0;
    Status = ZwRemoveIoCompletion(CompletionHandle,
                                  &CompletionKey,
                                  &CompletionContext,
                                  &IoStatusBlock,
                                  &Timeout);
    trace("ZwRemoveIoCompletion(empty) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_TIMEOUT);

Cleanup:
    if (PacketHandle != NULL)
        ZwClose(PacketHandle);
    if (EventHandle != NULL)
        ZwClose(EventHandle);
    ZwClose(CompletionHandle);
}
