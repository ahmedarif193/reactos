/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_read.h - WIM reading API (pure C)
 *
 */

#ifndef WIM_READ_H
#define WIM_READ_H

#include "wim_types.h"

/* Open and parse a WIM file (reads header, lookup table, XML) */
int wim_open(WimCtx* ctx, const char* filename);

/* Select/load an image's metadata (1-based index) */
int wim_select_image(WimCtx* ctx, int index);

/* Get root dentry for an image (1-based, must be selected first) */
const WimDentry* wim_get_root(const WimCtx* ctx, int index);

/* Read a blob by SHA-1, caller frees *data */
int wim_read_blob(WimCtx* ctx, const uint8_t sha1[20], uint8_t** data, size_t* data_size);

/* Extract a file to disk */
int wim_extract_file(WimCtx* ctx, const WimDentry* d, const char* dest_path);

/* Extract entire tree to a directory */
int wim_extract_tree(WimCtx* ctx, const WimDentry* d, const char* dest_dir);

/* Get XML as UTF-8 string (do not free - owned by ctx) */
const char* wim_get_xml(const WimCtx* ctx);

/* Verify integrity table */
int wim_verify_integrity(WimCtx* ctx);

/* Close and free */
void wim_close(WimCtx* ctx);

#endif /* WIM_READ_H */
