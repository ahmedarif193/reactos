/*
 * Copyright © 2026 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "pvr_ycbcr.h"

#include "util/macros.h"

/* number of slots occupied */
#define PVR_YCBCR_SLOTS 9

struct pvr_ycbcr_csc_table_entry {
   uint16_t value[32];
} PACKED;

struct pvr_ycbcr_csc_table {
   struct pvr_ycbcr_csc_table_entry slots[PVR_YCBCR_SLOTS];
} PACKED;

/* clang-format off */
static const uint16_t RGB_IDENTITY[] = {
   0x0000, 0x1ffc, 0x0000,
   0x0000, 0x0000, 0x1ffc,
   0x1ffc, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000,
};

static const uint16_t YCBCR_IDENTITY_FULL[] = {
   0x0000, 0x1ffc, 0x0000,
   0x0000, 0x0000, 0x1ffc,
   0x1ffc, 0x0000, 0x0000,
   0xfff0, 0x0000, 0xfff0,
};

static const uint16_t YCBCR_IDENTITY[] = {
   0x0000, 0x253e, 0x0000,
   0x0000, 0x0000, 0x2469,
   0x2469, 0x0000, 0x0000,
   0xfdb9, 0xfdac, 0xfdb9,
};

static const uint16_t YCBCR_709_FULL[] = {
   0x1ffc, 0x1ffc, 0x1ffc,
   0x0000, 0xfa02, 0x3b5a,
   0x325e, 0xf107, 0x0000,
   0xe6d1, 0x0a7b, 0xe253,
};

static const uint16_t YCBCR_709[] = {
   0x253e, 0x253e, 0x253e,
   0x0000, 0xf92e, 0x4390,
   0x3957, 0xeef5, 0x0000,
   0xe101, 0x099b, 0xdbe4,
};

static const uint16_t YCBCR_601_FULL[] = {
   0x1ffc, 0x1ffc, 0x1ffc,
   0x0000, 0xf4fe, 0x38ad,
   0x2cd8, 0xe929, 0x0000,
   0xe994, 0x10ed, 0xe3a9,
};

static const uint16_t YCBCR_601[] = {
   0x253e, 0x253e, 0x253e,
   0x0000, 0xf378, 0x4085,
   0x330c, 0xe5ff, 0x0000,
   0xe426, 0x10f0, 0xdd6a,
};

static const uint16_t YCBCR_2020_FULL[] = {
   0x1ffc, 0x1ffc, 0x1ffc,
   0x0000, 0xfabd, 0x3c2d,
   0x2f2a, 0xedba, 0x0000,
   0xe86b, 0x0bc5, 0xe1ea,
};

static const uint16_t YCBCR_2020[] = {
   0x253e, 0x253e, 0x253e,
   0x0000, 0xfa02, 0x4481,
   0x35b1, 0xeb32, 0x0000,
   0xe2d4, 0x0b12, 0xdb6c,
};
/* clang-format on */

#define fill_slot(table, entry) \
   memcpy(&table->slots[PVR_YCBCR_SLOT_##entry].value, entry, sizeof(entry))

void pvr_setup_static_yuv_csc_table(uint8_t *map, uint64_t offset)
{
   struct pvr_ycbcr_csc_table *table =
      (struct pvr_ycbcr_csc_table *)(&map[offset]);
   fill_slot(table, RGB_IDENTITY);
   fill_slot(table, YCBCR_IDENTITY_FULL);
   fill_slot(table, YCBCR_IDENTITY);
   fill_slot(table, YCBCR_709_FULL);
   fill_slot(table, YCBCR_709);
   fill_slot(table, YCBCR_601_FULL);
   fill_slot(table, YCBCR_601);
   fill_slot(table, YCBCR_2020_FULL);
   fill_slot(table, YCBCR_2020);
}
