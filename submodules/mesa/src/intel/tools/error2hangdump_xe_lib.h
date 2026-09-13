/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "error_decode_xe_lib.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void write_xe_vm_flags(FILE *f, uint32_t vm_flags);
void write_xe_buffer(FILE *f, uint64_t offset, const void *data,
                     uint64_t size, const struct xe_vma_properties *props, const char *name);
