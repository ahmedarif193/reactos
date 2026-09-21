/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/poolback.h
 * PURPOSE:     Pool backing provider interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

typedef struct _POOL_KM_LAYOUT
{
    PVOID NonPagedResidentBase;
    SIZE_T NonPagedResidentBytes;
    PVOID NonPagedExpansionBase;
    SIZE_T NonPagedExpansionBytes;
    PVOID ExecutableBase;
    SIZE_T ExecutableBytes;
    PVOID PagedBase;
    SIZE_T PagedBytes;
} POOL_KM_LAYOUT, *PPOOL_KM_LAYOUT;

VOID PoolBackQueryLayout(_Out_ PPOOL_KM_LAYOUT Layout);
VOID PoolBackDemandBacking(_Out_ PPOOL_BACKING Backing, _In_ BOOLEAN Executable);
VOID PoolBackResidentBacking(_Out_ PPOOL_BACKING Backing);
