/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "bi_print_common.h"
#include "compiler.h"

static const char *
bi_reg_op_name(enum bifrost_reg_op op)
{
   switch (op) {
   case BIFROST_OP_IDLE:
      return "idle";
   case BIFROST_OP_READ:
      return "read";
   case BIFROST_OP_WRITE:
      return "write";
   case BIFROST_OP_WRITE_LO:
      return "write lo";
   case BIFROST_OP_WRITE_HI:
      return "write hi";
   default:
      return "invalid";
   }
}

static void
print_debug_info(const struct nir_instr_debug_info *di, FILE *w)
{
   fprintf(w, "0x%08x", di->spirv_offset);

   if (di->filename)
      fprintf(w, " %s:%u:%u", di->filename, di->line, di->column);

   if (di->nir_line)
      fprintf(w, " nir:%u", di->nir_line);
}

void
bi_print_instr(const bi_instr *I, FILE *w)
{
   const struct nir_instr_debug_info *debug_info = I->debug_info;
   if (debug_info) {
      fprintf(w, "   // ");
      print_debug_info(debug_info, w);
      fprintf(w, "\n");
   }

   bi_print_instr_impl(I, w);
}

void
bi_print_slots(bi_registers *regs, FILE *fp)
{
   for (unsigned i = 0; i < 2; ++i) {
      if (regs->enabled[i])
         fprintf(fp, "slot %u: %u\n", i, regs->slot[i]);
   }

   if (regs->slot23.slot2) {
      fprintf(fp, "slot 2 (%s%s): %u\n", bi_reg_op_name(regs->slot23.slot2),
              regs->slot23.slot2 >= BIFROST_OP_WRITE ? " FMA" : "",
              regs->slot[2]);
   }

   if (regs->slot23.slot3) {
      fprintf(fp, "slot 3 (%s %s): %u\n", bi_reg_op_name(regs->slot23.slot3),
              regs->slot23.slot3_fma ? "FMA" : "ADD", regs->slot[3]);
   }
}

struct printer {
   FILE *w;

   struct nir_instr_debug_info last_debug_info;
};

static void
print_instr(const bi_instr *I, struct printer *p)
{
   const struct nir_instr_debug_info *debug_info = I->debug_info;
   if (debug_info) {
      bool changed = p->last_debug_info.spirv_offset != debug_info->spirv_offset;
      changed |= p->last_debug_info.nir_line != debug_info->nir_line;

      if (changed && debug_info->spirv_offset) {
         fprintf(p->w, "   // ");
         print_debug_info(debug_info, p->w);
         fprintf(p->w, "\n");
      }

      p->last_debug_info = *debug_info;
   }

   bi_print_instr_impl(I, p->w);
}

static void
print_tuple(bi_tuple *tuple, struct printer *p)
{
   bi_instr *ins[2] = {tuple->fma, tuple->add};

   for (unsigned i = 0; i < 2; ++i) {
      fprintf(p->w, (i == 0) ? "\t* " : "\t+ ");

      if (ins[i])
         print_instr(ins[i], p);
      else
         fprintf(p->w, "NOP\n");
   }
}

static void
print_clause(bi_clause *clause, struct printer *p)
{
   fprintf(p->w, "id(%u)", clause->scoreboard_id);

   if (clause->dependencies) {
      fprintf(p->w, " wait(");

      for (unsigned i = 0; i < 8; ++i) {
         if (clause->dependencies & (1 << i))
            fprintf(p->w, "%u ", i);
      }

      fprintf(p->w, ")");
   }

   fprintf(p->w, " %s", bi_flow_control_name(clause->flow_control));

   if (!clause->next_clause_prefetch)
      fprintf(p->w, " no_prefetch");

   if (clause->staging_barrier)
      fprintf(p->w, " osrb");

   if (clause->td)
      fprintf(p->w, " td");

   if (clause->pcrel_idx != ~0)
      fprintf(p->w, " pcrel(%u)", clause->pcrel_idx);

   fprintf(p->w, "\n");

   for (unsigned i = 0; i < clause->tuple_count; ++i)
      print_tuple(&clause->tuples[i], p);

   if (clause->constant_count) {
      for (unsigned i = 0; i < clause->constant_count; ++i)
         fprintf(p->w, "%" PRIx64 " ", clause->constants[i]);

      if (clause->branch_constant)
         fprintf(p->w, "*");

      fprintf(p->w, "\n");
   }

   fprintf(p->w, "\n");
}

static void
print_scoreboard_line(unsigned slot, const char *name, uint64_t mask,
                         FILE *fp)
{
   if (!mask)
      return;

   fprintf(fp, "slot %u %s:", slot, name);

   u_foreach_bit64(reg, mask)
      fprintf(fp, " r%" PRId64, reg);

   fprintf(fp, "\n");
}

static void
print_scoreboard(struct bi_scoreboard_state *state, FILE *fp)
{
   for (unsigned i = 0; i < BI_NUM_SLOTS; ++i) {
      print_scoreboard_line(i, "reads", state->read[i], fp);
      print_scoreboard_line(i, "writes", state->write[i], fp);
   }
}

static void
print_block(bi_block *block, FILE *fp)
{
   if (block->scheduled) {
      print_scoreboard(&block->scoreboard_in, fp);
      fprintf(fp, "\n");
   }

   fprintf(fp, "block%u {\n", block->index);

   struct printer printer = {.w = fp};

   if (block->scheduled) {
      bi_foreach_clause_in_block(block, clause)
         print_clause(clause, &printer);
   } else {
      bi_foreach_instr_in_block(block, ins)
         print_instr((bi_instr *)ins, &printer);
   }

   fprintf(fp, "}");

   if (block->successors[0]) {
      fprintf(fp, " -> ");

      bi_foreach_successor((block), succ)
         fprintf(fp, "block%u ", succ->index);
   }

   if (bi_num_predecessors(block)) {
      fprintf(fp, " from");

      bi_foreach_predecessor(block, pred)
         fprintf(fp, " block%u", (*pred)->index);
   }

   if (block->scheduled) {
      fprintf(fp, "\n");
      print_scoreboard(&block->scoreboard_out, fp);
   }

   fprintf(fp, "\n\n");
}

void
bi_print_shader(bi_context *ctx, FILE *fp)
{
   bi_foreach_block(ctx, block)
      print_block(block, fp);
}
