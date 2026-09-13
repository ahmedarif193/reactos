/*
 * Copyright © 2022 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define _3DPRIM_POINTLIST         0x01
#define _3DPRIM_LINELIST          0x02
#define _3DPRIM_LINESTRIP         0x03
#define _3DPRIM_TRILIST           0x04
#define _3DPRIM_TRISTRIP          0x05
#define _3DPRIM_TRIFAN            0x06
#define _3DPRIM_QUADLIST          0x07
#define _3DPRIM_QUADSTRIP         0x08
#define _3DPRIM_LINELIST_ADJ      0x09 /* G45+ */
#define _3DPRIM_LINESTRIP_ADJ     0x0A /* G45+ */
#define _3DPRIM_TRILIST_ADJ       0x0B /* G45+ */
#define _3DPRIM_TRISTRIP_ADJ      0x0C /* G45+ */
#define _3DPRIM_TRISTRIP_REVERSE  0x0D
#define _3DPRIM_POLYGON           0x0E
#define _3DPRIM_RECTLIST          0x0F
#define _3DPRIM_LINELOOP          0x10
#define _3DPRIM_POINTLIST_BF      0x11
#define _3DPRIM_LINESTRIP_CONT    0x12
#define _3DPRIM_LINESTRIP_BF      0x13
#define _3DPRIM_LINESTRIP_CONT_BF 0x14
#define _3DPRIM_TRIFAN_NOSTIPPLE  0x16
#define _3DPRIM_PATCHLIST(n) ({ assert(n > 0 && n <= 32); 0x20 + (n - 1); })
