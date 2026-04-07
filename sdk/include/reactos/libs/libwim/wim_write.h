/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_write.h - WIM writing API (pure C)
 *
 */

#ifndef WIM_WRITE_H
#define WIM_WRITE_H

#include "wim_types.h"

/* Create a new WIM file for writing */
int wim_create(WimCtx* ctx, const char* filename, int use_xpress);

/* Capture a directory tree into the WIM as a new image */
int wim_capture_tree(WimCtx* ctx, const char* source_dir,
                     const char* image_name, const char* image_desc);

/* Finalize: write lookup table, XML, integrity (if requested), header */
int wim_finalize(WimCtx* ctx, int write_integrity);

#endif /* WIM_WRITE_H */
