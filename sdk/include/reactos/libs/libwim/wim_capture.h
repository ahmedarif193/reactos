/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_capture.h - Directory tree walker for WIM capture (pure C)
 *
 */

#ifndef WIM_CAPTURE_H
#define WIM_CAPTURE_H

#include "wim_types.h"

/* Return 0 after taking ownership of data; return -1 to leave ownership
 * with the caller. */
typedef int (*wim_blob_writer_fn)(uint8_t* data, uint64_t size,
                                  void (*free_fn)(void*, size_t), void* free_arg,
                                  uint8_t sha1_out[20], void* user);

/* Capture a directory tree into a WimDentry tree, writing file blobs via callback. */
int wim_capture_dir(const char* source_dir, WimDentry* root,
                    wim_blob_writer_fn writer, void* user);

#endif /* WIM_CAPTURE_H */
