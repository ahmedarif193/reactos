/*
 * Copyright © 2023 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef _ISA_H_
#define _ISA_H_

#include "compiler/isaspec/isaspec.h"

struct etna_asm_result;
struct etna_inst;

#ifdef __cplusplus
extern "C" {
#endif

void isa_assemble_instruction(uint32_t *out, const struct etna_inst *instr);

extern struct etna_asm_result *isa_parse_str(const char *str);
extern struct etna_asm_result *isa_parse_file(const char *filepath);
extern void isa_asm_result_destroy(struct etna_asm_result *result);

#ifdef __cplusplus
}
#endif

#endif /* _ISA_H_ */
