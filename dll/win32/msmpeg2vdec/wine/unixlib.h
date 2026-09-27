/*
 * PROJECT:     ReactOS Media Foundation decoders
 * FILE:        dll/win32/msmpeg2vdec/wine/unixlib.h
 * PURPOSE:     Types Wine's transform interface expects from its Unix-call header
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

/* The transform backend runs in-process; main.c calls it directly. */
typedef UINT64 unixlib_handle_t;
