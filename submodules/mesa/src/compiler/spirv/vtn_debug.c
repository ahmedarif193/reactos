/*
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 *
 * File originally authored by: Faith Ekstrand <faith@gfxstrand.net>
 */

#include "vtn_private.h"

#ifdef HAVE_SPIRV_TOOLS
#include <spirv-tools/libspirv.h>
#endif /* HAVE_SPIRV_TOOLS */

void
spirv_print_asm(FILE *fp, const uint32_t *words, size_t word_count)
{
#ifdef HAVE_SPIRV_TOOLS
   spv_context ctx = spvContextCreate(SPV_ENV_UNIVERSAL_1_6);

   spv_binary_to_text_options_t options =
      SPV_BINARY_TO_TEXT_OPTION_INDENT |
      SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES;

   if (MESA_SPIRV_DEBUG(COLOR))
      options |= SPV_BINARY_TO_TEXT_OPTION_COLOR;

   if (MESA_SPIRV_DEBUG(OFFSETS))
      options |= SPV_BINARY_TO_TEXT_OPTION_SHOW_BYTE_OFFSET;

   spv_text text = NULL;
   spv_diagnostic diagnostic = NULL;
   spv_result_t res = spvBinaryToText(ctx, words, word_count, options,
                                      &text, &diagnostic);
   if (res == SPV_SUCCESS) {
      fprintf(fp, "SPIR-V assembly:\n");
      fwrite(text->str, 1, text->length, fp);
   } else {
      fprintf(fp, "Failed to disassemble SPIR-V:\n");
      spvDiagnosticPrint(diagnostic);
      spvDiagnosticDestroy(diagnostic);
   }

   spvTextDestroy(text);
#else
   fprintf(fp, "Cannot dump SPIR-V assembly. "
               "You need to build against SPIR-V tools.\n");
#endif /* HAVE_SPIRV_TOOLS */
}
