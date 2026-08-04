/* Adapted from Disarm 2d13d3f410a52daff1c5d8ef07d623332f372560; see LICENSE.disarm. */

#ifndef DA_DISARM64_H_
#define DA_DISARM64_H_


enum Da64Cond {
  /// Equals, zero set (Z=1)
  DA_EQ = 0x0,
  /// Not equals, zero clear (Z=0)
  DA_NE = 0x1,
  /// Higher or same, carry set (C=1)
  DA_CS = 0x2,
  /// Higher or same, carry set (C=1)
  DA_HS = 0x2,
  /// Lower, carry clear (C=0)
  DA_CC = 0x3,
  /// Lower, carry clear (C=0)
  DA_LO = 0x3,
  /// Minus, negate set (N=1)
  DA_MI = 0x4,
  /// Plus, negate clear (N=0)
  DA_PL = 0x5,
  /// Overflow set (V=1)
  DA_VS = 0x6,
  /// Overflow clear (V=0)
  DA_VC = 0x7,
  /// Higher (C=1 & Z=0)
  DA_HI = 0x8,
  /// Lower or same (C=0 | Z=1)
  DA_LS = 0x9,
  /// Greater than or equal (N=V)
  DA_GE = 0xa,
  /// Less than (N!=V)
  DA_LT = 0xb,
  /// Greater than (Z=0 & N=V)
  DA_GT = 0xc,
  /// Less than or equal (Z=1 | N!=V)
  DA_LE = 0xd,
  /// Always
  DA_AL = 0xe,
  /// Always (encoding never)
  DA_NV = 0xf,
};

typedef enum Da64Cond Da64Cond;

enum Da64PrfOp {
  DA_PRF_PLDL1KEEP = 0,
  DA_PRF_PLDL1STRM = 1,
  DA_PRF_PLDL2KEEP = 2,
  DA_PRF_PLDL2STRM = 3,
  DA_PRF_PLDL3KEEP = 4,
  DA_PRF_PLDL3STRM = 5,
  DA_PRF_PLIL1KEEP = 8,
  DA_PRF_PLIL1STRM = 9,
  DA_PRF_PLIL2KEEP = 10,
  DA_PRF_PLIL2STRM = 11,
  DA_PRF_PLIL3KEEP = 12,
  DA_PRF_PLIL3STRM = 13,
  DA_PRF_PSTL1KEEP = 16,
  DA_PRF_PSTL1STRM = 17,
  DA_PRF_PSTL2KEEP = 18,
  DA_PRF_PSTL2STRM = 19,
  DA_PRF_PSTL3KEEP = 20,
  DA_PRF_PSTL3STRM = 21,
};

typedef enum Da64PrfOp Da64PrfOp;

/* Decoder mnemonic and group definitions. */
#include "arm64-disarm-public.inc"

// Decoding API

enum Da64Ext {
  DA_EXT_UXTB = 0,
  DA_EXT_UXTH = 1,
  DA_EXT_UXTW = 2,
  DA_EXT_UXTX = 3,
  DA_EXT_SXTB = 4,
  DA_EXT_SXTH = 5,
  DA_EXT_SXTW = 6,
  DA_EXT_SXTX = 7,
  DA_EXT_LSL = 8,
  DA_EXT_LSR = 9,
  DA_EXT_ASR = 10,
  DA_EXT_ROR = 11,
};

enum Da64VectorArrangement {
  DA_VA_8B = 0,
  DA_VA_16B = 1,
  DA_VA_4H = 2,
  DA_VA_8H = 3,
  DA_VA_2S = 4,
  DA_VA_4S = 5,
  DA_VA_1D = 6,
  DA_VA_2D = 7,
  // special, only for group FP_HORZ_SCALAR
  DA_VA_2H = 8,
  // special
  DA_VA_1Q = 9,
};

enum Da64OpType {
  DA_OP_NONE = 0,
  /// General-purpose register. reg := Xn/31=ZR; reggp is valid.
  DA_OP_REGGP,
  /// General-purpose register increment. reg := Xn/31=ZR; reggp is valid.
  DA_OP_REGGPINC,
  /// Modified GP reg; reg := Xn/31=ZR; reggpext is valid.
  DA_OP_REGGPEXT,
  /// Stack Pointer; reggp is valid.
  DA_OP_REGSP,
  /// Scalar FP/vector reg; reg is vector reg; regfp is valid.
  DA_OP_REGFP,
  /// Vector; reg is vector reg; regvec is valid.
  DA_OP_REGVEC,
  /// Vector table, reg is vector reg; regvtbl is valid.
  DA_OP_REGVTBL,
  /// Vector element, reg is vector reg; regvidx is valid.
  DA_OP_REGVIDX,
  /// Vector table element, reg is vector reg; regvtblidx is valid.
  DA_OP_REGVTBLIDX,
  /// Memory with offset; reg is base or SP; offset in uimm16.
  DA_OP_MEMUOFF,
  /// Memory with offset; reg is base or SP; offset in simm16.
  DA_OP_MEMSOFF,
  /// Memory with pre-indexed offset; reg is base or SP; offset in simm16.
  DA_OP_MEMSOFFPRE,
  /// Memory with post-indexed offset; reg is base or SP; offset in simm16.
  DA_OP_MEMSOFFPOST,
  /// Memory with register offset; reg is base or SP; memreg is valid.
  DA_OP_MEMREG,
  /// Memory with post-incremented register; reg is base or SP; memreg is valid.
  DA_OP_MEMREGPOST,
  /// Memory with increment; reg is base or zero
  DA_OP_MEMINC,
  /// Condition code; cond is valid.
  DA_OP_COND,
  /// Prefetch operation; prfop is the operation.
  DA_OP_PRFOP,
  /// System register; sysreg encodes op1:CRn:CRm:op2.
  DA_OP_SYSREG,
  /// Small unsigned immediate <= 64, printed as decimal; stored in uimm16.
  DA_OP_IMMSMALL,
  /// Signed 16 bits immediate; value stored in simm16.
  DA_OP_SIMM,
  /// Unsigned 16 bits immediate; value stored in uimm16.
  DA_OP_UIMM,
  /// Unsigned 16 bits immediate; immshift is valid; value stored in uimm16.
  DA_OP_UIMMSHIFT,
  /// Large immediate, stored in imm64.
  DA_OP_IMMLARGE,
  /// Relative address, stored in imm64.
  DA_OP_RELADDR,
  /// Floating-point immediate, stored in float8.
  DA_OP_IMMFLOAT,
};

struct Da64Op {
  UCHAR type; // enum Da64OpType
  union {
    UCHAR reg;
    UCHAR prfop; // enum Da64PrfOp
    struct {
      UCHAR mask : 1; // 0 = LSL, 1 = MSL
      UCHAR shift : 7;
    } immshift;
  };
  union {
    struct {
      UCHAR sf : 1;
    } reggp;
    struct {
      UCHAR sf : 1;
      UCHAR ext : 7; // enum Da64Ext
      UCHAR shift;
    } reggpext;
    struct {
      UCHAR size;
    } regfp;
    struct {
      UCHAR va; // enum Da64VectorArrangement
    } regvec;
    struct {
      // 0=b, 1=h, 2=s, 3=d, (4=q), 5=2b, 6=4b, 7=2h
      UCHAR esize;
      UCHAR elem;
    } regvidx;
    struct {
      UCHAR va; // enum Da64VectorArrangement
      UCHAR cnt;
    } regvtbl;
    struct {
      UCHAR esize : 4;
      UCHAR elem : 4;
      UCHAR cnt;
    } regvtblidx;
    struct {
      UCHAR sc : 1;
      UCHAR ext : 4; // enum Da64Ext
      UCHAR shift : 3;
      UCHAR offreg;
    } memreg;
    USHORT sysreg;
    USHORT uimm16;
    SHORT simm16;
    UCHAR cond; // Da64Cond
  };
};

struct Da64Inst {
  USHORT mnem; // enum Da64InstKind
  struct Da64Op ops[5];
  union {
    ULONGLONG imm64;
    float float8;
  };
};

static enum Da64InstKind da64_classify(ULONG inst);
static VOID da64_decode(ULONG inst, struct Da64Inst* ddi);
static VOID da64_format(const struct Da64Inst* ddi, char* buf128);
/// Format decoded instruction with absolute address as base.
static VOID da64_format_abs(const struct Da64Inst* ddi, ULONGLONG addr, char* buf128);


#endif // DA_DISARM64_H_
