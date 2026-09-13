/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef __BI_DISASM_H
#define __BI_DISASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct bi_constants;
struct bifrost_regs;

void bi_disasm_fma(FILE *fp, unsigned bits, struct bifrost_regs *srcs,
                   struct bifrost_regs *next_regs, unsigned staging_register,
                   unsigned branch_offset, struct bi_constants *consts,
                   bool first);

void bi_disasm_add(FILE *fp, unsigned bits, struct bifrost_regs *srcs,
                   struct bifrost_regs *next_regs, unsigned staging_register,
                   unsigned branch_offset, struct bi_constants *consts,
                   bool first);

void bi_disasm_dest_fma(FILE *fp, struct bifrost_regs *next_regs, bool first);
void bi_disasm_dest_add(FILE *fp, struct bifrost_regs *next_regs, bool first);

void dump_src(FILE *fp, unsigned src, struct bifrost_regs srcs,
              unsigned branch_offset, struct bi_constants *consts, bool isFMA);

#endif
