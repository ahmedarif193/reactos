/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "error2hangdump_lib.h"
#include "error2hangdump_xe_lib.h"

#include "common/intel_hang_dump.h"
#include "error_decode_xe_lib.h"

void
write_xe_vm_flags(FILE *f,
                  uint32_t vm_flags)
{
   struct intel_hang_dump_block_vm_flags header = {
      .base = {
         .type = INTEL_HANG_DUMP_BLOCK_TYPE_VM_FLAGS,
      },
      .vm_flags = vm_flags,
   };
   fwrite(&header, sizeof(header), 1, f);
}

void
write_xe_buffer(FILE *f,
                uint64_t offset,
                const void *data,
                uint64_t size,
                const struct xe_vma_properties *props,
                const char *name)
{
   struct intel_hang_dump_block_bo header = {
      .base = {
         .type = INTEL_HANG_DUMP_BLOCK_TYPE_BO,
      },
      .props = {
         .mem_type = props->mem_type,
         .mem_permission = props->mem_permission,
         .mem_region = props->mem_region,
         .pat_index = props->pat_index,
         .cpu_caching = props->cpu_caching,
      },
      .offset  = offset,
      .size    = size,
   };
   snprintf(header.name, sizeof(header.name), "%s", name);

   fwrite(&header, sizeof(header), 1, f);
   fwrite(data, size, 1, f);
}
