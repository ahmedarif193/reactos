/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

/* Prints shared with the disassembler */

#include "bi_print_common.h"

const char *
bi_message_type_name(enum bifrost_message_type T)
{
   switch (T) {
   case BIFROST_MESSAGE_NONE:
      return "";
   case BIFROST_MESSAGE_VARYING:
      return "vary";
   case BIFROST_MESSAGE_ATTRIBUTE:
      return "attr";
   case BIFROST_MESSAGE_TEX:
      return "tex";
   case BIFROST_MESSAGE_VARTEX:
      return "vartex";
   case BIFROST_MESSAGE_LOAD:
      return "load";
   case BIFROST_MESSAGE_STORE:
      return "store";
   case BIFROST_MESSAGE_ATOMIC:
      return "atomic";
   case BIFROST_MESSAGE_BARRIER:
      return "barrier";
   case BIFROST_MESSAGE_BLEND:
      return "blend";
   case BIFROST_MESSAGE_TILE:
      return "tile";
   case BIFROST_MESSAGE_Z_STENCIL:
      return "z_stencil";
   case BIFROST_MESSAGE_ATEST:
      return "atest";
   case BIFROST_MESSAGE_JOB:
      return "job";
   case BIFROST_MESSAGE_64BIT:
      return "64";
   default:
      return "XXX reserved";
   }
}

const char *
bi_flow_control_name(enum bifrost_flow mode)
{
   switch (mode) {
   case BIFROST_FLOW_END:
      return "eos";
   case BIFROST_FLOW_NBTB_PC:
      return "nbb br_pc";
   case BIFROST_FLOW_NBTB_UNCONDITIONAL:
      return "nbb r_uncond";
   case BIFROST_FLOW_NBTB:
      return "nbb";
   case BIFROST_FLOW_BTB_UNCONDITIONAL:
      return "bb r_uncond";
   case BIFROST_FLOW_BTB_NONE:
      return "bb";
   case BIFROST_FLOW_WE_UNCONDITIONAL:
      return "we r_uncond";
   case BIFROST_FLOW_WE:
      return "we";
   default:
      return "XXX";
   }
}
