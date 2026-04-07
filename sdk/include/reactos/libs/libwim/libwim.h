/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Public umbrella header for libwim (WIM archive support)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * libwim is a clean-room implementation of the Microsoft Windows Imaging
 * Format. User-space applications that want to read, inspect, create or
 * modify .wim archives should include this header and link against
 * libwim (sdk/lib/libwim/). Future tools such as an in-tree DISM
 * replacement or an installer-side WIM extractor consume this API.
 *
 * For bootloader / constrained environments (freeldr, installers that run
 * before the CRT is available) use xpress_huff_decompress_static from
 * <xpress_huff.h> directly — it performs no dynamic allocation and only
 * requires a caller-provided XPRESS_DECOMPRESS_WORKSPACE_SIZE buffer.
 */

#ifndef REACTOS_LIBS_LIBWIM_H
#define REACTOS_LIBS_LIBWIM_H

#include "wim_format.h"
#include "wimcore.h"
#include "wim_types.h"
#include "wim_read.h"
#include "wim_write.h"
#include "wim_io.h"
#include "sha1.h"
#include "xpress_huff.h"

#endif /* REACTOS_LIBS_LIBWIM_H */
