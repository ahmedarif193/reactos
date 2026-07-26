/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Keyed mutex objects (D3DKMTCreate/Open/Acquire/Release/DestroyKeyedMutex)
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#pragma once

NTSTATUS DxgkKeyedMutexInitialize(VOID);
VOID DxgkKeyedMutexUninitialize(VOID);

NTSTATUS DxgkCreateKeyedMutex(_Inout_ D3DKMT_CREATEKEYEDMUTEX *pData);
NTSTATUS DxgkCreateKeyedMutex2(_Inout_ D3DKMT_CREATEKEYEDMUTEX2 *pData, _In_ KPROCESSOR_MODE AccessMode);
NTSTATUS DxgkOpenKeyedMutex(_Inout_ D3DKMT_OPENKEYEDMUTEX *pData);
NTSTATUS DxgkOpenKeyedMutex2(_Inout_ D3DKMT_OPENKEYEDMUTEX2 *pData, _In_ KPROCESSOR_MODE AccessMode);
NTSTATUS DxgkDestroyKeyedMutex(_In_ CONST D3DKMT_DESTROYKEYEDMUTEX *pData);
NTSTATUS DxgkAcquireKeyedMutex(_Inout_ D3DKMT_ACQUIREKEYEDMUTEX *pData);
NTSTATUS DxgkAcquireKeyedMutex2(_Inout_ D3DKMT_ACQUIREKEYEDMUTEX2 *pData, _In_ KPROCESSOR_MODE AccessMode);
NTSTATUS DxgkReleaseKeyedMutex(_Inout_ D3DKMT_RELEASEKEYEDMUTEX *pData);
NTSTATUS DxgkReleaseKeyedMutex2(_Inout_ D3DKMT_RELEASEKEYEDMUTEX2 *pData, _In_ KPROCESSOR_MODE AccessMode);

VOID DxgkKeyedMutexProcessCleanup(_In_ PEPROCESS Process);
