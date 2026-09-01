/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.1 full-screen-exclusive block contract tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"

#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#endif

C_ASSERT(sizeof(D3DKMT_SETFSEBLOCK) == 20);
C_ASSERT(FIELD_OFFSET(D3DKMT_SETFSEBLOCK, AdapterLuid) == 0);
C_ASSERT(FIELD_OFFSET(D3DKMT_SETFSEBLOCK, hAdapter) == 8);
C_ASSERT(FIELD_OFFSET(D3DKMT_SETFSEBLOCK, VidPnSourceId) == 12);
C_ASSERT(FIELD_OFFSET(D3DKMT_SETFSEBLOCK, Flags) == 16);
C_ASSERT(sizeof(D3DKMT_QUERYFSEBLOCK) == 20);
C_ASSERT(FIELD_OFFSET(D3DKMT_QUERYFSEBLOCK, AdapterLuid) == 0);
C_ASSERT(FIELD_OFFSET(D3DKMT_QUERYFSEBLOCK, hAdapter) == 8);
C_ASSERT(FIELD_OFFSET(D3DKMT_QUERYFSEBLOCK, VidPnSourceId) == 12);
C_ASSERT(FIELD_OFFSET(D3DKMT_QUERYFSEBLOCK, Flags) == 16);

static void
Test_SetFseBlockUnsupported(void)
{
    D3DKMT_SETFSEBLOCK Before;
    D3DKMT_SETFSEBLOCK Data;
    PFND3DKMT_SETFSEBLOCK SetFseBlock;
    NTSTATUS Status;

    SetFseBlock = (PFND3DKMT_SETFSEBLOCK)LoadD3DKMTProc("D3DKMTSetFSEBlock");
    if (SetFseBlock == NULL)
    {
        skip("D3DKMTSetFSEBlock not exported by gdi32.dll\n");
        return;
    }

    Status = SetFseBlock(NULL);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTSetFSEBlock(NULL) returned 0x%08lx\n", (unsigned long)Status);

    memset(&Data, 0xa5, sizeof(Data));
    Before = Data;
    Status = SetFseBlock(&Data);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTSetFSEBlock(data) returned 0x%08lx\n", (unsigned long)Status);
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "D3DKMTSetFSEBlock modified its input\n");

    Status = SetFseBlock((D3DKMT_SETFSEBLOCK *)(ULONG_PTR)1);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTSetFSEBlock(invalid pointer) returned 0x%08lx\n", (unsigned long)Status);
}

static void
Test_QueryFseBlockUnsupported(void)
{
    D3DKMT_QUERYFSEBLOCK Before;
    D3DKMT_QUERYFSEBLOCK Data;
    PFND3DKMT_QUERYFSEBLOCK QueryFseBlock;
    NTSTATUS Status;

    QueryFseBlock = (PFND3DKMT_QUERYFSEBLOCK)LoadD3DKMTProc("D3DKMTQueryFSEBlock");
    if (QueryFseBlock == NULL)
    {
        skip("D3DKMTQueryFSEBlock not exported by gdi32.dll\n");
        return;
    }

    Status = QueryFseBlock(NULL);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTQueryFSEBlock(NULL) returned 0x%08lx\n", (unsigned long)Status);

    memset(&Data, 0x5a, sizeof(Data));
    Before = Data;
    Status = QueryFseBlock(&Data);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTQueryFSEBlock(data) returned 0x%08lx\n", (unsigned long)Status);
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "D3DKMTQueryFSEBlock modified its output on failure\n");

    Status = QueryFseBlock((D3DKMT_QUERYFSEBLOCK *)(ULONG_PTR)1);
    ok(Status == STATUS_NOT_SUPPORTED, "D3DKMTQueryFSEBlock(invalid pointer) returned 0x%08lx\n", (unsigned long)Status);
}

START_TEST(fseblock)
{
    Test_SetFseBlockUnsupported();
    Test_QueryFseBlockUnsupported();
}
