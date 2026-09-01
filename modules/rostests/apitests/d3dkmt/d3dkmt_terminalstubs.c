/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Win11 terminal-status D3DKMT contract tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"

#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#endif

typedef NTSTATUS (APIENTRY *PFN_CREATEHWCONTEXT)(D3DKMT_CREATEHWCONTEXT *Data);
typedef NTSTATUS (APIENTRY *PFN_DESTROYHWCONTEXT)(const D3DKMT_DESTROYHWCONTEXT *Data);
typedef NTSTATUS (APIENTRY *PFN_SETCOLORTRANSFORM)(D3DKMT_SET_COLORSPACE_TRANSFORM *Data);

#ifdef _WIN64
C_ASSERT(sizeof(D3DKMT_CREATEHWCONTEXT) == 40);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATEHWCONTEXT, hHwContext) == 32);
C_ASSERT(sizeof(D3DKMT_SET_COLORSPACE_TRANSFORM) == 32);
C_ASSERT(FIELD_OFFSET(D3DKMT_SET_COLORSPACE_TRANSFORM, pColorSpaceTransform) == 24);
#else
C_ASSERT(sizeof(D3DKMT_CREATEHWCONTEXT) == 28);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATEHWCONTEXT, hHwContext) == 24);
C_ASSERT(sizeof(D3DKMT_SET_COLORSPACE_TRANSFORM) == 24);
C_ASSERT(FIELD_OFFSET(D3DKMT_SET_COLORSPACE_TRANSFORM, pColorSpaceTransform) == 20);
#endif
C_ASSERT(sizeof(D3DKMT_DESTROYHWCONTEXT) == 4);

static void
Test_CreateHwContextTerminalStatus(void)
{
    D3DKMT_CREATEHWCONTEXT Before;
    D3DKMT_CREATEHWCONTEXT Data;
    PFN_CREATEHWCONTEXT Function;
    NTSTATUS Status;

    Function = (PFN_CREATEHWCONTEXT)LoadD3DKMTProc("D3DKMTCreateHwContext");
    ok(Function != NULL, "D3DKMTCreateHwContext is not exported by gdi32.dll\n");
    if (Function == NULL)
        return;

    Status = Function(NULL);
    ok(Status == STATUS_NOT_IMPLEMENTED, "D3DKMTCreateHwContext(NULL) returned 0x%08lx\n", (unsigned long)Status);

    memset(&Data, 0xa5, sizeof(Data));
    Before = Data;
    Status = Function(&Data);
    ok(Status == STATUS_NOT_IMPLEMENTED, "D3DKMTCreateHwContext(data) returned 0x%08lx\n", (unsigned long)Status);
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "D3DKMTCreateHwContext modified its input\n");

    Status = Function((D3DKMT_CREATEHWCONTEXT *)(ULONG_PTR)1);
    ok(Status == STATUS_NOT_IMPLEMENTED, "D3DKMTCreateHwContext(invalid pointer) returned 0x%08lx\n", (unsigned long)Status);
}

static void
Test_DestroyHwContextTerminalStatus(void)
{
    D3DKMT_DESTROYHWCONTEXT Before;
    D3DKMT_DESTROYHWCONTEXT Data;
    PFN_DESTROYHWCONTEXT Function;
    NTSTATUS Status;

    Function = (PFN_DESTROYHWCONTEXT)LoadD3DKMTProc("D3DKMTDestroyHwContext");
    ok(Function != NULL, "D3DKMTDestroyHwContext is not exported by gdi32.dll\n");
    if (Function == NULL)
        return;

    Status = Function(NULL);
    ok(Status == STATUS_NOT_IMPLEMENTED, "D3DKMTDestroyHwContext(NULL) returned 0x%08lx\n", (unsigned long)Status);

    memset(&Data, 0x5a, sizeof(Data));
    Before = Data;
    Status = Function(&Data);
    ok(Status == STATUS_NOT_IMPLEMENTED, "D3DKMTDestroyHwContext(data) returned 0x%08lx\n", (unsigned long)Status);
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "D3DKMTDestroyHwContext modified its input\n");

    Status = Function((D3DKMT_DESTROYHWCONTEXT *)(ULONG_PTR)1);
    ok(Status == STATUS_NOT_IMPLEMENTED, "D3DKMTDestroyHwContext(invalid pointer) returned 0x%08lx\n", (unsigned long)Status);
}

static void
Test_SetMonitorColorSpaceTransformTerminalStatus(void)
{
    D3DKMT_SET_COLORSPACE_TRANSFORM Before;
    D3DKMT_SET_COLORSPACE_TRANSFORM Data;
    PFN_SETCOLORTRANSFORM Function;
    NTSTATUS Status;

    Function = (PFN_SETCOLORTRANSFORM)LoadD3DKMTProc("D3DKMTSetMonitorColorSpaceTransform");
    ok(Function != NULL, "D3DKMTSetMonitorColorSpaceTransform is not exported by gdi32.dll\n");
    if (Function == NULL)
        return;

    Status = Function(NULL);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTSetMonitorColorSpaceTransform(NULL) returned 0x%08lx\n", (unsigned long)Status);

    memset(&Data, 0x3c, sizeof(Data));
    Before = Data;
    Status = Function(&Data);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTSetMonitorColorSpaceTransform(data) returned 0x%08lx\n", (unsigned long)Status);
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "D3DKMTSetMonitorColorSpaceTransform modified its input\n");

    Status = Function((D3DKMT_SET_COLORSPACE_TRANSFORM *)(ULONG_PTR)1);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTSetMonitorColorSpaceTransform(invalid pointer) returned 0x%08lx\n", (unsigned long)Status);
}

START_TEST(terminalstubs)
{
    Test_CreateHwContextTerminalStatus();
    Test_DestroyHwContextTerminalStatus();
    Test_SetMonitorColorSpaceTransformTerminalStatus();
}
