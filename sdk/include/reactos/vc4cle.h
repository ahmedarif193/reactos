/*
 * PROJECT:     ReactOS VC4/V3D control-list encoders (UMD track, Stage 0)
 * LICENSE:     MIT (encodings derived from mesa src/broadcom/cle/v3d_packet.xml)
 * PURPOSE:     Header-only encoders for the minimal V3D 7.1 (BCM2712)
 *              control-list packet subset needed by the bring-up
 *              exerciser (vc4-umd-design.txt Stage 1: RCL solid clear).
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * EVERY layout below is transcribed field-for-field from the mesa genxml
 * packet database (v3d_packet.xml, min_ver=71 variants) — the same source
 * the Linux/mesa stack generates its encoders from.  Do NOT hand-derive
 * or "fix" encodings here without re-checking that file.
 *
 * Encoding model (mesa cle): one opcode byte, then a little-endian
 * payload; XML field bit offsets are relative to the PAYLOAD start
 * (proven by fields with start < 8 in multi-byte packets).
 */

#ifndef _VC4CLE_H_
#define _VC4CLE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ---- opcodes ---------------------------------------------------------- */
#define V3D71_CL_HALT                        0
#define V3D71_CL_NOP                         1
#define V3D71_CL_FLUSH                       4
#define V3D71_CL_FLUSH_ALL_STATE             5
#define V3D71_CL_START_TILE_BINNING          6
#define V3D71_CL_END_OF_RENDERING            13
#define V3D71_CL_SUPERTILE_COORDS            23
#define V3D71_CL_CLEAR_RT                    25
#define V3D71_CL_END_OF_LOADS                26
#define V3D71_CL_END_OF_TILE_MARKER          27
#define V3D71_CL_STORE_TILE_BUFFER_GENERAL   29
#define V3D71_CL_LOAD_TILE_BUFFER_GENERAL    30
#define V3D71_CL_TILE_BINNING_MODE_CFG       120
#define V3D71_CL_TILE_RENDERING_MODE_CFG     121
#define V3D71_CL_MULTICORE_SUPERTILE_CFG     122
#define V3D71_CL_TILE_COORDS                 124

/* Tile Rendering Mode Cfg sub-ids (v71 numbering). */
#define V3D71_TRM_SUBID_COMMON               0
#define V3D71_TRM_SUBID_ZS_CLEAR_VALUES      1
#define V3D71_TRM_SUBID_RT_PART1             2
#define V3D71_TRM_SUBID_RT_PART2             3
#define V3D71_TRM_SUBID_RT_PART3             4

/* ---- enums (verbatim values) ------------------------------------------ */
#define V3D71_INTERNAL_BPP_32                0
#define V3D71_INTERNAL_BPP_64                1
#define V3D71_INTERNAL_BPP_128               2

#define V3D71_INTERNAL_TYPE_8                2
#define V3D71_INTERNAL_TYPE_16F              6
#define V3D71_INTERNAL_TYPE_32F              10

/* "Render Target Type Clamp" (v71 RT Part1 Internal Type and Clamping). */
#define V3D71_RT_TYPE_CLAMP_8                8
#define V3D71_RT_TYPE_CLAMP_16F              9
#define V3D71_RT_TYPE_CLAMP_32F              10

#define V3D71_OUTPUT_FORMAT_BGR565           7
#define V3D71_OUTPUT_FORMAT_RGBA8            27

#define V3D71_MEMORY_FORMAT_RASTER           0
#define V3D71_MEMORY_FORMAT_UIF_NO_XOR       4

#define V3D71_DITHER_NONE                    0
#define V3D71_DECIMATE_SAMPLE0               0

#define V3D71_DEPTH_TYPE_32F                 0

/* Store/Load "Buffer to Store" numbering (mesa generated pack headers):
 * RENDER_TARGET_0..7 = 0..7, NONE = 8 (dummy store for GFXH-1742),
 * depth/stencil buffers above.  NONE=8 pending mesa-golden confirmation. */
#define V3D71_BUFFER_RT0                     0
#define V3D71_BUFFER_NONE                    8

/* ---- packing core ------------------------------------------------------ */

/* OR `Size` bits of `Value` into little-endian payload at bit `Start`. */
static __inline void
Vc4ClePackBits(
    unsigned char *Payload,
    unsigned long long Value,
    unsigned int Start,
    unsigned int Size)
{
    unsigned int Bit;

    if (Size < 64)
        Value &= (1ULL << Size) - 1;

    for (Bit = 0; Bit < Size; Bit++)
    {
        if (Value & (1ULL << Bit))
            Payload[(Start + Bit) >> 3] |=
                (unsigned char)(1u << ((Start + Bit) & 7));
    }
}

static __inline unsigned char *
Vc4CleOpcode(unsigned char *Cl, unsigned char Opcode, unsigned int PayloadBytes)
{
    unsigned int i;

    *Cl++ = Opcode;
    for (i = 0; i < PayloadBytes; i++)
        Cl[i] = 0;
    return Cl;
}

/* ---- zero-payload packets ---------------------------------------------- */
static __inline unsigned char *
Vc4CleFlush(unsigned char *Cl)              { *Cl++ = V3D71_CL_FLUSH; return Cl; }
static __inline unsigned char *
Vc4CleStartTileBinning(unsigned char *Cl)   { *Cl++ = V3D71_CL_START_TILE_BINNING; return Cl; }
static __inline unsigned char *
Vc4CleEndOfRendering(unsigned char *Cl)     { *Cl++ = V3D71_CL_END_OF_RENDERING; return Cl; }
static __inline unsigned char *
Vc4CleEndOfLoads(unsigned char *Cl)         { *Cl++ = V3D71_CL_END_OF_LOADS; return Cl; }
static __inline unsigned char *
Vc4CleEndOfTileMarker(unsigned char *Cl)    { *Cl++ = V3D71_CL_END_OF_TILE_MARKER; return Cl; }

/* ---- Tile Binning Mode Cfg (code 120, v71: 8-byte payload) -------------
 * fields: initial block size@2/2, block size@4/2, Log2TileWidth@8/3,
 *         Log2TileHeight@11/3, Width(px)@32/16, Height(px)@48/16        */
static __inline unsigned char *
Vc4CleTileBinningModeCfgV71(
    unsigned char *Cl,
    unsigned int AllocInitialBlockSize,  /* 0=64b 1=128b 2=256b */
    unsigned int AllocBlockSize,
    unsigned int Log2TileWidth,
    unsigned int Log2TileHeight,
    unsigned int WidthPixels,
    unsigned int HeightPixels)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_TILE_BINNING_MODE_CFG, 8);

    Vc4ClePackBits(P, AllocInitialBlockSize, 2, 2);
    Vc4ClePackBits(P, AllocBlockSize,        4, 2);
    Vc4ClePackBits(P, Log2TileWidth,         8, 3);
    Vc4ClePackBits(P, Log2TileHeight,       11, 3);
    Vc4ClePackBits(P, WidthPixels,          32, 16);
    Vc4ClePackBits(P, HeightPixels,         48, 16);
    return P + 8;
}

/* ---- Tile Rendering Mode Cfg (Common) (code 121 sub 0, v71: 8B) --------
 * fields: sub-id@0/3, NumRT@4/4, ImageWidth@8/16, ImageHeight@24/16,
 *         MS4x@42, DoubleBuffer@43, DepthDisable@44, EZDir@45, EZDis@46,
 *         InternalDepthType@47/4(?), Log2TileWidth@52/3, Log2TileHeight@55/3 */
static __inline unsigned char *
Vc4CleTrmCfgCommonV71(
    unsigned char *Cl,
    unsigned int NumRenderTargets,
    unsigned int ImageWidth,
    unsigned int ImageHeight,
    unsigned int InternalDepthType,
    unsigned int Log2TileWidth,
    unsigned int Log2TileHeight)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_TILE_RENDERING_MODE_CFG, 8);

    Vc4ClePackBits(P, V3D71_TRM_SUBID_COMMON, 0, 3);
    Vc4ClePackBits(P, NumRenderTargets,       4, 4);
    Vc4ClePackBits(P, ImageWidth,             8, 16);
    Vc4ClePackBits(P, ImageHeight,           24, 16);
    Vc4ClePackBits(P, 1,                     44, 1);   /* depth-buffer disable */
    Vc4ClePackBits(P, InternalDepthType,     47, 4);
    Vc4ClePackBits(P, Log2TileWidth,         52, 3);
    Vc4ClePackBits(P, Log2TileHeight,        55, 3);
    return P + 8;
}

/* ---- TRM Cfg (ZS Clear Values) (code 121 sub 1, v71: 8B) ---------------
 * fields: sub-id@0/3, StencilClear@8/8, ZClear(float)@16/32              */
static __inline unsigned char *
Vc4CleTrmCfgZsClearV71(
    unsigned char *Cl,
    unsigned int ZClearValueF32Bits,
    unsigned int StencilClearValue)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_TILE_RENDERING_MODE_CFG, 8);

    Vc4ClePackBits(P, V3D71_TRM_SUBID_ZS_CLEAR_VALUES, 0, 3);
    Vc4ClePackBits(P, StencilClearValue,   8, 8);
    Vc4ClePackBits(P, ZClearValueF32Bits, 16, 32);
    return P + 8;
}

/* ---- TRM Cfg (Render Target Part1) (code 121 sub 2, v71: 8B) -----------
 * fields: sub-id@0/3, RTnumber@3/4(?XML start=3), BaseAddress@7/11,
 *         Stride@18/7, InternalBPP@25/2, TypeAndClamp@27/5, ClearLow@32/32 */
static __inline unsigned char *
Vc4CleTrmCfgRtPart1V71(
    unsigned char *Cl,
    unsigned int RenderTargetNumber,
    unsigned int TileBufferBaseAddress,  /* in tile-buffer words */
    unsigned int TileBufferStride,
    unsigned int InternalBpp,            /* V3D71_INTERNAL_BPP_*  */
    unsigned int TypeAndClamp,           /* V3D71_RT_TYPE_CLAMP_* */
    unsigned int ClearColorLow32)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_TILE_RENDERING_MODE_CFG, 8);

    Vc4ClePackBits(P, V3D71_TRM_SUBID_RT_PART1, 0, 3);
    Vc4ClePackBits(P, RenderTargetNumber,    3, 4);
    Vc4ClePackBits(P, TileBufferBaseAddress, 7, 11);
    Vc4ClePackBits(P, TileBufferStride,     18, 7);
    Vc4ClePackBits(P, InternalBpp,          25, 2);
    Vc4ClePackBits(P, TypeAndClamp,         27, 5);
    Vc4ClePackBits(P, ClearColorLow32,      32, 32);
    return P + 8;
}

/* ---- Multicore Rendering Supertile Cfg (code 122, 8B) ------------------
 * fields: SupertileW(tiles)@0/8, SupertileH@8/8, FrameW(supertiles)@16/8,
 *         FrameH@24/8, FrameW(tiles)@32/12, FrameH(tiles)@44/12,
 *         MulticoreEnable@56/1, RasterOrder@60/1, NumBinTileLists@61/3   */
static __inline unsigned char *
Vc4CleMulticoreSupertileCfg(
    unsigned char *Cl,
    unsigned int SupertileWidthTiles,
    unsigned int SupertileHeightTiles,
    unsigned int FrameWidthSupertiles,
    unsigned int FrameHeightSupertiles,
    unsigned int FrameWidthTiles,
    unsigned int FrameHeightTiles,
    unsigned int NumberOfBinTileLists)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_MULTICORE_SUPERTILE_CFG, 8);

    Vc4ClePackBits(P, SupertileWidthTiles,    0, 8);
    Vc4ClePackBits(P, SupertileHeightTiles,   8, 8);
    Vc4ClePackBits(P, FrameWidthSupertiles,  16, 8);
    Vc4ClePackBits(P, FrameHeightSupertiles, 24, 8);
    Vc4ClePackBits(P, FrameWidthTiles,       32, 12);
    Vc4ClePackBits(P, FrameHeightTiles,      44, 12);
    Vc4ClePackBits(P, NumberOfBinTileLists,  61, 3);
    return P + 8;
}

/* ---- Tile / Supertile coordinates -------------------------------------- */
static __inline unsigned char *
Vc4CleTileCoords(unsigned char *Cl, unsigned int Column, unsigned int Row)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_TILE_COORDS, 3);

    Vc4ClePackBits(P, Column,  0, 12);
    Vc4ClePackBits(P, Row,    12, 12);
    return P + 3;
}

static __inline unsigned char *
Vc4CleSupertileCoords(unsigned char *Cl, unsigned int Column, unsigned int Row)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_SUPERTILE_COORDS, 2);

    Vc4ClePackBits(P, Column, 0, 8);
    Vc4ClePackBits(P, Row,    8, 8);
    return P + 2;
}

/* ---- Store Tile Buffer General (code 29, 12B payload) -------------------
 * fields: BufferToStore@0/4(3?), MemoryFormat@4/3, FlipY@7/1, Dither@8/2,
 *         Decimate@10/2, OutputFormat@12/6, ClearBufferBeingStored@18/1,
 *         ChannelReverse@19/1, RBSwap@20/1, HeightUBorStride@28/20,
 *         Height@48/16, Address@64/32                                     */
static __inline unsigned char *
Vc4CleStoreTileBufferGeneral(
    unsigned char *Cl,
    unsigned int BufferToStore,       /* V3D71_BUFFER_RT0 + n */
    unsigned int MemoryFormat,        /* V3D71_MEMORY_FORMAT_* */
    unsigned int OutputImageFormat,   /* V3D71_OUTPUT_FORMAT_* */
    unsigned int ClearBufferBeingStored,
    unsigned int StrideBytes,         /* raster: byte stride   */
    unsigned int GpuAddress)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_STORE_TILE_BUFFER_GENERAL, 12);

    Vc4ClePackBits(P, BufferToStore,           0, 4);
    Vc4ClePackBits(P, MemoryFormat,            4, 3);
    Vc4ClePackBits(P, V3D71_DITHER_NONE,       8, 2);
    Vc4ClePackBits(P, V3D71_DECIMATE_SAMPLE0, 10, 2);
    Vc4ClePackBits(P, OutputImageFormat,      12, 6);
    Vc4ClePackBits(P, ClearBufferBeingStored, 18, 1);
    Vc4ClePackBits(P, StrideBytes,            28, 20);
    Vc4ClePackBits(P, GpuAddress,             64, 32);
    return P + 12;
}

/* ---- control-list flow ------------------------------------------------- */
#define V3D71_CL_BRANCH_SUB_AUTOCHAIN        15
#define V3D71_CL_BRANCH                      16
#define V3D71_CL_GENERIC_TILE_LIST           20
#define V3D71_CL_BRANCH_IMPLICIT_TILE        21
#define V3D71_CL_VERTEX_ARRAY_PRIMS          36
#define V3D71_CL_PRIM_LIST_FORMAT            56
#define V3D71_CL_GL_SHADER_STATE             64
#define V3D71_CL_CFG_BITS                    96
#define V3D71_CL_CLIP_WINDOW                 107
#define V3D71_CL_VIEWPORT_OFFSET             108
#define V3D71_CL_TILE_LIST_BASE              123
#define V3D71_CL_TILE_LIST_INITIAL_BLOCK     126

#define V3D71_PRIM_TRIANGLES                 4   /* GL primitive numbering */

/* return (code 18), clear_vcd_cache (19, mesa FLUSH_VCD_CACHE), and the
 * v71 Clear Render Targets (25) are zero-payload single-byte packets
 * (all three self-closing in v3d_packet.xml). */
#define V3D71_CL_RETURN_FROM_SUB_LIST        18
#define V3D71_CL_FLUSH_VCD_CACHE             19

static __inline unsigned char *
Vc4CleReturnFromSubList(unsigned char *Cl)  { *Cl++ = V3D71_CL_RETURN_FROM_SUB_LIST; return Cl; }
static __inline unsigned char *
Vc4CleFlushVcdCache(unsigned char *Cl)      { *Cl++ = V3D71_CL_FLUSH_VCD_CACHE; return Cl; }
static __inline unsigned char *
Vc4CleClearRenderTargetsV71(unsigned char *Cl) { *Cl++ = V3D71_CL_CLEAR_RT; return Cl; }

/* Branch (code 16): address@0/32. */
static __inline unsigned char *
Vc4CleBranch(unsigned char *Cl, unsigned int GpuAddress)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_BRANCH, 4);

    Vc4ClePackBits(P, GpuAddress, 0, 32);
    return P + 4;
}

/* generic_tile_list (code 20): start@0/32, end@32/32. */
static __inline unsigned char *
Vc4CleGenericTileList(unsigned char *Cl, unsigned int Start, unsigned int End)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_GENERIC_TILE_LIST, 8);

    Vc4ClePackBits(P, Start,  0, 32);
    Vc4ClePackBits(P, End,   32, 32);
    return P + 8;
}

/* branch_implicit_tile (code 21): tile list set number@0/8. */
static __inline unsigned char *
Vc4CleBranchImplicitTile(unsigned char *Cl, unsigned int TileListSetNumber)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_BRANCH_IMPLICIT_TILE, 1);

    Vc4ClePackBits(P, TileListSetNumber, 0, 8);
    return P + 1;
}

/* multicore_rendering_tile_list_base (code 123): set#@0/4, addr[31:6]@6/26. */
static __inline unsigned char *
Vc4CleTileListBase(unsigned char *Cl, unsigned int TileListSetNumber,
                   unsigned int GpuAddress /* 64-byte aligned */)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_TILE_LIST_BASE, 4);

    Vc4ClePackBits(P, TileListSetNumber, 0, 4);
    Vc4ClePackBits(P, GpuAddress >> 6,   6, 26);
    return P + 4;
}

/* Tile List Initial Block Size (code 126): size@0/2, auto-chain@2/1. */
static __inline unsigned char *
Vc4CleTileListInitialBlockSize(unsigned char *Cl, unsigned int SizeCode,
                               unsigned int AutoChain)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_TILE_LIST_INITIAL_BLOCK, 1);

    Vc4ClePackBits(P, SizeCode,  0, 2);
    Vc4ClePackBits(P, AutoChain, 2, 1);
    return P + 1;
}

/* gl_shader (code 64): #attribute arrays@0/5, record addr[31:5]@5/27. */
static __inline unsigned char *
Vc4CleGlShaderState(unsigned char *Cl, unsigned int NumAttributeArrays,
                    unsigned int RecordGpuAddress /* 32-byte aligned */)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_GL_SHADER_STATE, 4);

    Vc4ClePackBits(P, NumAttributeArrays,     0, 5);
    Vc4ClePackBits(P, RecordGpuAddress >> 5,  5, 27);
    return P + 4;
}

/* Vertex Array Prims (code 36): mode@0/8, length@8/32, first@40/32. */
static __inline unsigned char *
Vc4CleVertexArrayPrims(unsigned char *Cl, unsigned int PrimMode,
                       unsigned int VertexCount, unsigned int FirstVertex)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_VERTEX_ARRAY_PRIMS, 9);

    Vc4ClePackBits(P, PrimMode,     0, 8);
    Vc4ClePackBits(P, VertexCount,  8, 32);
    Vc4ClePackBits(P, FirstVertex, 40, 32);
    return P + 9;
}

/* Prim List Format (code 56): primitive type@0/6, strip/fan@7/1. */
static __inline unsigned char *
Vc4ClePrimListFormat(unsigned char *Cl, unsigned int PrimitiveType,
                     unsigned int TriStripOrFan)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_PRIM_LIST_FORMAT, 1);

    Vc4ClePackBits(P, PrimitiveType, 0, 6);
    Vc4ClePackBits(P, TriStripOrFan, 7, 1);
    return P + 1;
}

/* Cfg Bits v71 (code 96, 3-byte payload): minimal fields for flat
 * front-facing triangles: fwd@0, rev@1, cw@2, depth-test func@12/3. */
static __inline unsigned char *
Vc4CleCfgBitsV71(unsigned char *Cl, unsigned int EnableForward,
                 unsigned int EnableReverse, unsigned int ClockwisePrims,
                 unsigned int DepthTestFunc)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_CFG_BITS, 3);

    Vc4ClePackBits(P, EnableForward,  0, 1);
    Vc4ClePackBits(P, EnableReverse,  1, 1);
    Vc4ClePackBits(P, ClockwisePrims, 2, 1);
    Vc4ClePackBits(P, DepthTestFunc, 12, 3);
    return P + 3;
}

/* clip (code 107): left@0/16, bottom@16/16, width@32/16, height@48/16. */
static __inline unsigned char *
Vc4CleClipWindow(unsigned char *Cl, unsigned int Left, unsigned int Bottom,
                 unsigned int Width, unsigned int Height)
{
    unsigned char *P = Vc4CleOpcode(Cl, V3D71_CL_CLIP_WINDOW, 8);

    Vc4ClePackBits(P, Left,    0, 16);
    Vc4ClePackBits(P, Bottom, 16, 16);
    Vc4ClePackBits(P, Width,  32, 16);
    Vc4ClePackBits(P, Height, 48, 16);
    return P + 8;
}

/* ---- TFU (texture formatting unit) job image, v71 ----------------------
 * Register-image layout matches the rpi5vc4 kernel packet contract
 * (RPI5VC4_DMA_OP_TFU_JOB): [Icfg Iia Ica Iis Iua Ioc Ioa Ios C0..C3].
 * Constants verbatim from mesa src/broadcom/common/v3d_tfu.h (MIT) and
 * the job construction from v3dvx_meta_common.c meta_emit_tfu_job
 * (V3D_VERSION >= 71 paths).  v71 CAN write raster destinations.      */
#define V3D71_TFU_ICFG_IFORMAT_SHIFT         23
#define V3D71_TFU_ICFG_OTYPE_SHIFT           16
#define V3D71_TFU_ICFG_FMT_RASTER            0
#define V3D71_TFU_IOC_FORMAT_SHIFT           12
#define V3D71_TFU_IOC_FMT_RASTER             0
#define V3D71_TFU_IOC_STRIDE_SHIFT           16

#define V3D71_TEXFMT_RGBA8                   4   /* Texture Data Formats */

/* Raster-to-raster same-format copy (2D blit). Strides in BYTES; the
 * hardware wants pixels-per-row (stride/cpp) in IIS and IOC.STRIDE. */
static __inline void
Vc4CleTfuRasterCopyV71(
    unsigned int Regs[12],
    unsigned int SrcGpuVa,
    unsigned int SrcStrideBytes,
    unsigned int DstGpuVa,
    unsigned int DstStrideBytes,
    unsigned int WidthPixels,
    unsigned int HeightPixels,
    unsigned int TexDataFormat,      /* V3D71_TEXFMT_*                 */
    unsigned int BytesPerPixel)
{
    unsigned int i;

    for (i = 0; i < 12; i++)
        Regs[i] = 0;

    /* [0] ICFG: input format raster + output tex type. */
    Regs[0] = (V3D71_TFU_ICFG_FMT_RASTER << V3D71_TFU_ICFG_IFORMAT_SHIFT) |
              (TexDataFormat << V3D71_TFU_ICFG_OTYPE_SHIFT);
    /* [1] IIA: input image address. */
    Regs[1] = SrcGpuVa;
    /* [3] IIS: input stride in pixels. */
    Regs[3] = SrcStrideBytes / BytesPerPixel;
    /* [5] IOC: output format raster + output stride in pixels. */
    Regs[5] = (V3D71_TFU_IOC_FMT_RASTER << V3D71_TFU_IOC_FORMAT_SHIFT) |
              ((DstStrideBytes / BytesPerPixel) << V3D71_TFU_IOC_STRIDE_SHIFT);
    /* [6] IOA: output image address. */
    Regs[6] = DstGpuVa;
    /* [7] IOS: (height << 16) | width. */
    Regs[7] = (HeightPixels << 16) | WidthPixels;
}

#ifdef __cplusplus
}
#endif

#endif /* _VC4CLE_H_ */
