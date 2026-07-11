/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     V3D 7.1 control-list encoder unit tests (UMD Stage 0).
 *              Pure computation — no adapter required; expected bytes are
 *              hand-computed independently from the mesa v3d_packet.xml
 *              field layouts (start/size attributes), NOT from the
 *              encoder under test.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"
#include <reactos/vc4cle.h>

START_TEST(cle)
{
    unsigned char Cl[64];
    unsigned char *End;

    /* Zero-payload opcodes are single bytes. */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleFlush(Cl);
    ok(End == Cl + 1 && Cl[0] == 4, "Flush: got len %d op %u\n",
       (int)(End - Cl), Cl[0]);

    End = Vc4CleEndOfRendering(Cl);
    ok(End == Cl + 1 && Cl[0] == 13, "end_render: got len %d op %u\n",
       (int)(End - Cl), Cl[0]);

    /*
     * Tile coords (code 124): column@0/12, row@12/12.
     * col=5 row=3 => payload bits: 5 | (3<<12) = 0x3005
     * little-endian 3 bytes: 05 30 00
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleTileCoords(Cl, 5, 3);
    ok(End == Cl + 4, "tile_coords length %d\n", (int)(End - Cl));
    ok(Cl[0] == 124 && Cl[1] == 0x05 && Cl[2] == 0x30 && Cl[3] == 0x00,
       "tile_coords bytes %02x %02x %02x %02x\n", Cl[0], Cl[1], Cl[2], Cl[3]);

    /*
     * Supertile coords (code 23): column@0/8, row@8/8.
     * col=2 row=7 => 02 07
     */
    End = Vc4CleSupertileCoords(Cl, 2, 7);
    ok(End == Cl + 3 && Cl[0] == 23 && Cl[1] == 0x02 && Cl[2] == 0x07,
       "supertile_coords bytes %02x %02x %02x\n", Cl[0], Cl[1], Cl[2]);

    /*
     * Tile Binning Mode Cfg v71 (code 120, 8-byte payload):
     * initial block size=1 @2/2 => 0x04; block size=2 @4/2 => 0x20;
     * log2 tile w=3 @8/3; log2 tile h=3 @11/3 => byte1 = 3 | 3<<3 = 0x1B;
     * width=640 @32/16 => bytes4-5 = 80 02; height=480 @48/16 => E0 01.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleTileBinningModeCfgV71(Cl, 1, 2, 3, 3, 640, 480);
    ok(End == Cl + 9, "binning cfg length %d\n", (int)(End - Cl));
    ok(Cl[0] == 120, "binning cfg opcode %u\n", Cl[0]);
    ok(Cl[1] == 0x24, "binning cfg byte0 %02x (expected 24)\n", Cl[1]);
    ok(Cl[2] == 0x1B, "binning cfg byte1 %02x (expected 1b)\n", Cl[2]);
    ok(Cl[3] == 0x00 && Cl[4] == 0x00, "binning cfg pad %02x %02x\n", Cl[3], Cl[4]);
    ok(Cl[5] == 0x80 && Cl[6] == 0x02, "binning width bytes %02x %02x\n", Cl[5], Cl[6]);
    ok(Cl[7] == 0xE0 && Cl[8] == 0x01, "binning height bytes %02x %02x\n", Cl[7], Cl[8]);

    /*
     * TRM Cfg Common v71 (code 121 sub 0): NumRT=1 @4/4 => byte0 = 0 | 1<<4
     * = 0x10; width=640 @8/16 => bytes1-2 = 80 02; height=480 @24/16 =>
     * bytes3-4 = E0 01; depth disable @44 => byte5 |= 0x10;
     * depth type 0 @47; log2 tw=3 @52/3 => byte6 |= 3<<4 = 0x30;
     * log2 th=3 @55/3 => bit55 in byte6 bit7 + byte7 bits0-1:
     * 3 = 0b011 => bit55=1, bit56=1, bit57=0 => byte6 |= 0x80, byte7 = 0x01.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleTrmCfgCommonV71(Cl, 1, 640, 480, V3D71_DEPTH_TYPE_32F, 3, 3);
    ok(End == Cl + 9, "trm common length %d\n", (int)(End - Cl));
    ok(Cl[0] == 121 && Cl[1] == 0x10, "trm common op/byte0 %02x %02x\n", Cl[0], Cl[1]);
    ok(Cl[2] == 0x80 && Cl[3] == 0x02, "trm width %02x %02x\n", Cl[2], Cl[3]);
    ok(Cl[4] == 0xE0 && Cl[5] == 0x01, "trm height bytes %02x %02x (exp e0 01)\n",
       Cl[4], Cl[5]);
    ok(Cl[6] == 0x10, "trm depth-disable byte %02x (exp 10)\n", Cl[6]);
    ok(Cl[7] == 0xB0 && Cl[8] == 0x01, "trm tile-size bytes %02x %02x (exp b0 01)\n",
       Cl[7], Cl[8]);

    /*
     * TRM RT Part1 v71 (code 121 sub 2): sub=2 @0/3, RT#=0 @3/4,
     * base addr=0 @7/11, stride=1 @18/7 => byte2 |= 1<<2 = 0x04;
     * bpp=0 @25/2; type/clamp=8 @27/5 => bits27-31 = 8 => byte3 |= 8<<3
     * = 0x40; clear low = 0x11223344 @32/32 => bytes4-7 = 44 33 22 11.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleTrmCfgRtPart1V71(Cl, 0, 0, 1,
                                 V3D71_INTERNAL_BPP_32,
                                 V3D71_RT_TYPE_CLAMP_8,
                                 0x11223344);
    ok(End == Cl + 9, "rt part1 length %d\n", (int)(End - Cl));
    ok(Cl[1] == 0x02, "rt part1 byte0 %02x (exp 02)\n", Cl[1]);
    ok(Cl[3] == 0x04, "rt part1 stride byte %02x (exp 04)\n", Cl[3]);
    ok(Cl[4] == 0x40, "rt part1 type byte %02x (exp 40)\n", Cl[4]);
    ok(Cl[5] == 0x44 && Cl[6] == 0x33 && Cl[7] == 0x22 && Cl[8] == 0x11,
       "rt part1 clear bytes %02x %02x %02x %02x\n", Cl[5], Cl[6], Cl[7], Cl[8]);

    /*
     * Store Tile Buffer General (code 29, 12B): buffer=0 @0/4,
     * memfmt raster=0 @4/3, dither0 @8/2, decimate0 @10/2, fmt rgba8=27
     * @12/6 => bits12-17 = 27 => byte1 |= (27&0xF)<<4 = 0xB0,
     * byte2 |= 27>>4 = 0x01; clear=1 @18 => byte2 |= 0x04;
     * stride=2560 @28/20 => 2560 = 0xA00 => bits28.. => byte3 |= 0<<4?
     * 0xA00 << 28 in LE: bits28-47; 0xA00 = 1010 0000 0000b;
     * bit28-31 = 0, byte4 = 0xA0, byte5 = 0x00;
     * address 0x01000000 @64/32 => bytes8-11 = 00 00 00 01.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleStoreTileBufferGeneral(Cl, V3D71_BUFFER_RT0,
                                       V3D71_MEMORY_FORMAT_RASTER,
                                       V3D71_OUTPUT_FORMAT_RGBA8,
                                       1, 2560, 0x01000000);
    ok(End == Cl + 13, "store length %d\n", (int)(End - Cl));
    ok(Cl[0] == 29 && Cl[1] == 0x00, "store op/byte0 %02x %02x\n", Cl[0], Cl[1]);
    ok(Cl[2] == 0xB0, "store fmt byte1 %02x (exp b0)\n", Cl[2]);
    ok(Cl[3] == 0x05, "store fmt/clear byte2 %02x (exp 05)\n", Cl[3]);
    ok(Cl[4] == 0x00 && Cl[5] == 0xA0 && Cl[6] == 0x00,
       "store stride bytes %02x %02x %02x (exp 00 a0 00)\n", Cl[4], Cl[5], Cl[6]);
    ok(Cl[9] == 0x00 && Cl[10] == 0x00 && Cl[11] == 0x00 && Cl[12] == 0x01,
       "store address bytes %02x %02x %02x %02x\n",
       Cl[9], Cl[10], Cl[11], Cl[12]);

    /* Branch (code 16): address 0x01234560 => 60 45 23 01. */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleBranch(Cl, 0x01234560);
    ok(End == Cl + 5 && Cl[0] == 16 &&
       Cl[1] == 0x60 && Cl[2] == 0x45 && Cl[3] == 0x23 && Cl[4] == 0x01,
       "branch bytes %02x %02x %02x %02x %02x\n",
       Cl[0], Cl[1], Cl[2], Cl[3], Cl[4]);

    /*
     * Vertex Array Prims (code 36): mode=4 @0/8, count=3 @8/32,
     * first=0 @40/32 => 04 03 00 00 00 00 00 00 00.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleVertexArrayPrims(Cl, V3D71_PRIM_TRIANGLES, 3, 0);
    ok(End == Cl + 10 && Cl[0] == 36 && Cl[1] == 0x04 && Cl[2] == 0x03 &&
       Cl[3] == 0 && Cl[9] == 0,
       "vertex array prims bytes %02x %02x %02x\n", Cl[0], Cl[1], Cl[2]);

    /*
     * gl_shader (code 64): 2 attribute arrays @0/5, record 0x01000020
     * (32-byte aligned) => addr>>5 = 0x00080001 at bit 5:
     * value = 2 | (0x00080001 << 5) = 2 | 0x01000020 = 0x01000022
     * => bytes 22 00 00 01.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleGlShaderState(Cl, 2, 0x01000020);
    ok(End == Cl + 5 && Cl[0] == 64 &&
       Cl[1] == 0x22 && Cl[2] == 0x00 && Cl[3] == 0x00 && Cl[4] == 0x01,
       "gl_shader bytes %02x %02x %02x %02x %02x\n",
       Cl[0], Cl[1], Cl[2], Cl[3], Cl[4]);

    /*
     * tile_list_base (code 123): set 1 @0/4, addr 0x01000040 (64-aligned)
     * => addr>>6 = 0x40001 at bit 6 => 1 | (0x40001<<6) = 0x01000041
     * => bytes 41 00 00 01.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleTileListBase(Cl, 1, 0x01000040);
    ok(End == Cl + 5 && Cl[0] == 123 &&
       Cl[1] == 0x41 && Cl[2] == 0x00 && Cl[3] == 0x00 && Cl[4] == 0x01,
       "tile_list_base bytes %02x %02x %02x %02x %02x\n",
       Cl[0], Cl[1], Cl[2], Cl[3], Cl[4]);

    /*
     * Cfg Bits v71 (code 96): fwd=1, rev=1, cw=1, depth func 7 (ALWAYS)
     * => byte0 = 0x07, byte1 = 7<<4 = 0x70, byte2 = 0.
     */
    memset(Cl, 0xAA, sizeof(Cl));
    End = Vc4CleCfgBitsV71(Cl, 1, 1, 1, 7);
    ok(End == Cl + 4 && Cl[0] == 96 &&
       Cl[1] == 0x07 && Cl[2] == 0x70 && Cl[3] == 0x00,
       "cfg bits bytes %02x %02x %02x %02x\n", Cl[0], Cl[1], Cl[2], Cl[3]);

    /*
     * TFU raster copy (v71): 64x64 RGBA8, strides 256B (64 px):
     * ICFG = (0<<23) | (4<<16) = 0x00040000;
     * IIS = 64; IOC = (0<<12)|(64<<16) = 0x00400000;
     * IOS = (64<<16)|64 = 0x00400040.
     */
    {
        unsigned int Regs[12];

        Vc4CleTfuRasterCopyV71(Regs, 0x01001000, 256, 0x01002000, 256,
                               64, 64, V3D71_TEXFMT_RGBA8, 4);
        ok(Regs[0] == 0x00040000, "tfu icfg %08x\n", Regs[0]);
        ok(Regs[1] == 0x01001000 && Regs[6] == 0x01002000,
           "tfu iia/ioa %08x %08x\n", Regs[1], Regs[6]);
        ok(Regs[3] == 64, "tfu iis %08x\n", Regs[3]);
        ok(Regs[5] == 0x00400000, "tfu ioc %08x\n", Regs[5]);
        ok(Regs[7] == 0x00400040, "tfu ios %08x\n", Regs[7]);
        ok(Regs[2] == 0 && Regs[4] == 0 && Regs[8] == 0,
           "tfu unused regs %08x %08x %08x\n", Regs[2], Regs[4], Regs[8]);
    }
}
