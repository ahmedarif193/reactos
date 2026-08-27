/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Boot video virtual frame-buffer handoff tests
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTSTATUS
NTAPI
InbvSetVirtualFrameBuffer(
    _In_opt_ PVOID VirtualFrameBuffer);

START_TEST(InbvVirtualFrameBuffer)
{
    NTSTATUS Status;

    Status = InbvSetVirtualFrameBuffer(NULL);
    trace("InbvSetVirtualFrameBuffer(NULL) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
