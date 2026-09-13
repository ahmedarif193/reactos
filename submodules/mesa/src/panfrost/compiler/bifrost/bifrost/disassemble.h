/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef __BI_DISASSEMBLE_H
#define __BI_DISASSEMBLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void disassemble_bifrost(FILE *fp, const void *code, size_t size, bool verbose);

#endif
