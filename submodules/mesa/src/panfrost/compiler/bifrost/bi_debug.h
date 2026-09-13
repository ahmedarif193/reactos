/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef __bi_debug_h__
#define __bi_debug_h__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIFROST_DBG_MSGS       0x0001
#define BIFROST_DBG_SHADERS    0x0002
#define BIFROST_DBG_SHADERDB   0x0004
#define BIFROST_DBG_VERBOSE    0x0008
#define BIFROST_DBG_INTERNAL   0x0010
#define BIFROST_DBG_NOSCHED    0x0020
#define BIFROST_DBG_INORDER    0x0040
#define BIFROST_DBG_NOVALIDATE 0x0080
#define BIFROST_DBG_NOOPT      0x0100
#define BIFROST_DBG_NOIDVS     0x0200
#define BIFROST_DBG_NOSB       0x0400
#define BIFROST_DBG_NOPRELOAD  0x0800
#define BIFROST_DBG_SPILL      0x1000
#define BIFROST_DBG_NOPSCHED   0x2000
#define BIFROST_DBG_NOSSARA    0x4000
#define BIFROST_DBG_STATSABS   0x8000
#define BIFROST_DBG_STATSFULL 0x10000
#define BIFROST_DBG_DEBUGINFO 0x20000

extern unsigned bifrost_debug;

void bifrost_init_debug_options(void);

bool bifrost_will_dump_shaders(void);
bool bifrost_want_debug_info(void);

#ifdef __cplusplus
} /* extern C */
#endif

#endif
