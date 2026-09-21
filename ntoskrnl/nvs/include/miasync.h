/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/miasync.h
 * PURPOSE:     Asynchronous memory manager operation definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

typedef struct _MI_ASYNC_DRAIN
{
    LIST_ENTRY Link;
    VOID (*Complete)(_In_opt_ PVOID Context);
    PVOID Context;
} MI_ASYNC_DRAIN, *PMI_ASYNC_DRAIN;
