/* Adapted from Disarm 2d13d3f410a52daff1c5d8ef07d623332f372560; see LICENSE.disarm. */
#include "arm64-disarm.h"

/* classify.c */
#define DA64_CLASSIFIER
#include "arm64-disarm-private.inc"
#undef DA64_CLASSIFIER

static enum Da64InstKind da64_classify(ULONG inst) {
  return da64_classify_impl(inst);
}

/* decode.c */
static ULONGLONG sext(ULONGLONG imm, unsigned bits) {
  ULONGLONG sign = (ULONGLONG)1 << (bits - 1);
  return imm & sign ? (imm ^ sign) - sign : imm;
}

static unsigned clz(ULONG v, unsigned sz) {
  return v ? __builtin_clz(v) + sz - 32 : sz;
}

static unsigned ctz(ULONG v) { return v ? __builtin_ctz(v) : 32; }

static ULONGLONG immlogical(unsigned sf, unsigned N, unsigned immr,
                           unsigned imms) {
  if (!N && imms == 0x3f)
    return 0;
  unsigned len = 31 - __builtin_clz((N << 6) | (~imms & 0x3f));
  unsigned levels = (1 << len) - 1;
  unsigned s = imms & levels;
  unsigned r = immr & levels;
  unsigned esize = 1 << len;
  ULONGLONG welem = ((ULONGLONG)1 << (s + 1)) - 1;
  // ROR(welem, r) as bits(esize)
  if (r)
    welem = (welem >> r) | (welem << (esize - r));
  if (esize < 64)
    welem &= ((ULONGLONG)1 << esize) - 1;
  // Replicate(ROR(welem, r))
  ULONGLONG wmask = 0;
  for (unsigned i = 0; i < (!sf ? 32 : 64); i += esize)
    wmask |= welem << i;
  return wmask;
}

static struct Da64Op OPreggp(unsigned idx, BOOLEAN sf) {
  return (struct Da64Op){DA_OP_REGGP, .reg = idx, .reggp = {sf}};
}
static struct Da64Op OPreggpinc(unsigned idx) {
  return (struct Da64Op){DA_OP_REGGPINC, .reg = idx, .reggp = {TRUE}};
}

static struct Da64Op OPreggpsp(unsigned idx, BOOLEAN sf) {
  return (struct Da64Op){
      idx != 31 ? DA_OP_REGGP : DA_OP_REGSP,
      .reg = idx,
      .reggp = {sf},
  };
}

static struct Da64Op OPreggpmaysp(BOOLEAN maysp, unsigned idx, BOOLEAN sf) {
  return (struct Da64Op){
      idx < 31 || !maysp ? DA_OP_REGGP : DA_OP_REGSP,
      .reg = idx,
      .reggp = {sf},
  };
}
static struct Da64Op OPreggpprf(BOOLEAN isprf, unsigned idx, BOOLEAN sf) {
  return (struct Da64Op){
      isprf ? DA_OP_PRFOP : DA_OP_REGGP,
      .reg = idx,
      .reggp = {sf},
  };
}

static struct Da64Op OPreggpext(unsigned idx, BOOLEAN sf, enum Da64Ext ext,
                                unsigned shift) {
  return (struct Da64Op){DA_OP_REGGPEXT, .reg = idx,
                         .reggpext = {sf, ext, shift}};
}
static struct Da64Op OPregfp(unsigned idx, unsigned size) {
  return (struct Da64Op){DA_OP_REGFP, .reg = idx, .regfp = {size}};
}
static struct Da64Op OPregvec(unsigned idx, unsigned esize, BOOLEAN Q) {
  return (struct Da64Op){DA_OP_REGVEC, .reg = idx,
                         .regvec = {(esize << 1) + Q}};
}
static struct Da64Op OPregvidx(unsigned idx, unsigned esize, unsigned elem) {
  return (struct Da64Op){DA_OP_REGVIDX, .reg = idx, .regvidx = {esize, elem}};
}
static struct Da64Op OPregvtbl(unsigned idx, unsigned esize, BOOLEAN Q,
                               unsigned cnt) {
  return (struct Da64Op){DA_OP_REGVTBL, .reg = idx,
                         .regvtbl = {(esize << 1) + Q, cnt}};
}
static struct Da64Op OPregvtblidx(unsigned idx, unsigned esize, unsigned elem,
                                  unsigned cnt) {
  return (struct Da64Op){DA_OP_REGVTBLIDX, .reg = idx,
                         .regvtblidx = {esize, elem, cnt}};
}
static struct Da64Op OPmemuoff(unsigned idx, USHORT off) {
  return (struct Da64Op){DA_OP_MEMUOFF, .reg = idx, .uimm16 = off};
}
static struct Da64Op OPmemsoff(unsigned idx, SHORT off) {
  return (struct Da64Op){DA_OP_MEMSOFF, .reg = idx, .simm16 = off};
}
static struct Da64Op OPmemsoffpre(unsigned idx, SHORT off) {
  return (struct Da64Op){DA_OP_MEMSOFFPRE, .reg = idx, .simm16 = off};
}
static struct Da64Op OPmemsoffpost(unsigned idx, SHORT off) {
  return (struct Da64Op){DA_OP_MEMSOFFPOST, .reg = idx, .simm16 = off};
}
static struct Da64Op OPmemreg(unsigned idx, unsigned offreg, enum Da64Ext ext,
                              BOOLEAN scale, unsigned shift) {
  return (struct Da64Op){DA_OP_MEMREG, .reg = idx,
                         .memreg = {scale, ext, shift, offreg}};
}
static struct Da64Op OPmemregsimdpost(unsigned idx, unsigned offreg,
                                      unsigned constoff) {
  if (offreg == 31)
    return (struct Da64Op){DA_OP_MEMSOFFPOST, .reg = idx, .simm16 = constoff};
  return (struct Da64Op){DA_OP_MEMREGPOST, .reg = idx,
                         .memreg = {0, DA_EXT_UXTX, 0, offreg}};
}
static struct Da64Op OPmeminc(unsigned idx) {
  return (struct Da64Op){DA_OP_MEMINC, .reg = idx, .uimm16 = 0};
}
static struct Da64Op OPimmsmall(unsigned imm6) {
  return (struct Da64Op){DA_OP_IMMSMALL, {0}, .uimm16 = imm6};
}
static struct Da64Op OPsimm(SHORT imm) {
  return (struct Da64Op){DA_OP_SIMM, {0}, .simm16 = imm};
}
static struct Da64Op OPuimm(USHORT imm) {
  return (struct Da64Op){DA_OP_UIMM, {0}, .uimm16 = imm};
}
static struct Da64Op OPuimmshift(USHORT imm, BOOLEAN msl, unsigned shift) {
  return (struct Da64Op){DA_OP_UIMMSHIFT, .immshift = {msl, shift},
                         .uimm16 = imm};
}
static struct Da64Op OPreladdr(struct Da64Inst* ddi, LONGLONG imm) {
  ddi->imm64 = imm;
  return (struct Da64Op){DA_OP_RELADDR, {0}, .uimm16 = 0};
}
static struct Da64Op OPimmlogical(struct Da64Inst* ddi, unsigned sf, unsigned N,
                                  unsigned immr, unsigned imms) {
  ddi->imm64 = immlogical(sf, N, immr, imms);
  return (struct Da64Op){DA_OP_IMMLARGE, {0}, .uimm16 = sf};
}
static struct Da64Op OPimmsimdmask(struct Da64Inst* ddi, UCHAR imm8) {
  ULONGLONG res = 0;
  for (unsigned i = 0; i < 8; i++)
    res += imm8 & (1 << i) ? (ULONGLONG)0xff << (i * 8) : 0;
  ddi->imm64 = res;
  return (struct Da64Op){DA_OP_IMMLARGE, {0}, .uimm16 = 1};
}
static struct Da64Op OPimmfloatzero(struct Da64Inst* ddi) {
  ddi->float8 = 0.0f;
  return (struct Da64Op){DA_OP_IMMFLOAT, {0}, .uimm16 = 0x100};
}
static struct Da64Op OPimmfloat(struct Da64Inst* ddi, UCHAR imm8) {
  ULONG res = (ULONG)(imm8 & 0x80) << 24;
  res |= imm8 & 0x40 ? 0x3e000000 : 0x40000000;
  res |= (imm8 & 0x3f) << 19;
  // clang-format off
  ddi->float8 = (union { ULONG i; float f; }){.i = res}.f;
  // clang-format on
  return (struct Da64Op){DA_OP_IMMFLOAT, {0}, .uimm16 = imm8};
}
static struct Da64Op OPsysreg(unsigned reg) {
  return (struct Da64Op){DA_OP_SYSREG, {0}, .sysreg = reg};
}
static struct Da64Op OPcond(unsigned cond) {
  return (struct Da64Op){DA_OP_COND, {0}, .cond = cond};
}

static VOID da64_decode(ULONG inst, struct Da64Inst* ddi) {
  for (unsigned i = 0; i < sizeof(ddi->ops) / sizeof(ddi->ops[0]); i++)
    ddi->ops[i] = (struct Da64Op){0};
  unsigned mnem = da64_classify(inst);
  ddi->mnem = mnem;
  switch (DA64_GROUP(mnem)) {
    // Needs variables mnem, inst, and ddi.
#define DA64_DECODER
#include "arm64-disarm-private.inc"
#undef DA64_DECODER
  }
}

/* format.c */
static char* da_strpcat4(char* restrict dst, const char* str, unsigned len) {
  for (unsigned i = 0; i < 4; i++)
    dst[i] = str[i];
  return dst + len;
}
static char* da_strpcat8(char* restrict dst, const char* str, unsigned len) {
  for (unsigned i = 0; i < 8; i++)
    dst[i] = str[i];
  return dst + len;
}
static char* da_strpcat12(char* restrict dst, const char* str, unsigned len) {
  for (unsigned i = 0; i < 12; i++)
    dst[i] = str[i];
  return dst + len;
}

static char* da_strpcatimmdecstr(char* restrict dst, unsigned imm,
                                 unsigned skip) {
  static const char* tbl =
      " #0\0  #1\0  #2\0  #3\0  #4\0  #5\0  #6\0  #7\0 "
      " #8\0  #9\0  #10\0 #11\0 #12\0 #13\0 #14\0 #15\0"
      " #16\0 #17\0 #18\0 #19\0 #20\0 #21\0 #22\0 #23\0"
      " #24\0 #25\0 #26\0 #27\0 #28\0 #29\0 #30\0 #31\0"
      " #32\0 #33\0 #34\0 #35\0 #36\0 #37\0 #38\0 #39\0"
      " #40\0 #41\0 #42\0 #43\0 #44\0 #45\0 #46\0 #47\0"
      " #48\0 #49\0 #50\0 #51\0 #52\0 #53\0 #54\0 #55\0"
      " #56\0 #57\0 #58\0 #59\0 #60\0 #61\0 #62\0 #63\0 #64";
  return da_strpcat4(dst, tbl + 5 * imm + skip, 3 - skip + (imm >= 10));
}

static char* da_strpcatuimmhex(char* restrict dst, ULONGLONG imm) {
  unsigned numbytes = 16 - (__builtin_clzll(imm | 1) / 4);
  unsigned idx = numbytes;
  do {
    dst[--idx] = "0123456789abcdef"[imm % 16];
    imm /= 16;
  } while (imm);
  return dst + numbytes;
}

static char* da_strpcatsimmhex16(char* restrict dst, SHORT imm,
                                 unsigned skip) {
  ULONGLONG uimm = imm < 0 ? -(ULONGLONG)imm : (ULONGLONG)imm;
  if (imm < 0)
    dst = da_strpcat8(dst, &"], #-0x    "[skip], 7 - skip);
  else
    dst = da_strpcat8(dst, &"], #0x     "[skip], 6 - skip);
  return da_strpcatuimmhex(dst, uimm);
}

static char* da_strpcatreggp(char* restrict dst, unsigned sf, unsigned idx) {
  const char* wstr = "w0\0 w1\0 w2\0 w3\0 w4\0 w5\0 w6\0 w7\0 "
                     "w8\0 w9\0 w10\0w11\0w12\0w13\0w14\0w15\0"
                     "w16\0w17\0w18\0w19\0w20\0w21\0w22\0w23\0"
                     "w24\0w25\0w26\0w27\0w28\0w29\0w30\0wzr";
  const char* xstr = "x0\0 x1\0 x2\0 x3\0 x4\0 x5\0 x6\0 x7\0 "
                     "x8\0 x9\0 x10\0x11\0x12\0x13\0x14\0x15\0"
                     "x16\0x17\0x18\0x19\0x20\0x21\0x22\0x23\0"
                     "x24\0x25\0x26\0x27\0x28\0x29\0x30\0xzr";
  return da_strpcat4(dst, (sf ? xstr : wstr) + idx * 4, idx >= 10 ? 3 : 2);
}

static char* da_strpcatreggpsp(char* restrict dst, unsigned sf, unsigned idx) {
  const char* wstr = "w0\0 w1\0 w2\0 w3\0 w4\0 w5\0 w6\0 w7\0 "
                     "w8\0 w9\0 w10\0w11\0w12\0w13\0w14\0w15\0"
                     "w16\0w17\0w18\0w19\0w20\0w21\0w22\0w23\0"
                     "w24\0w25\0w26\0w27\0w28\0w29\0w30\0wsp";
  const char* xstr = "x0\0 x1\0 x2\0 x3\0 x4\0 x5\0 x6\0 x7\0 "
                     "x8\0 x9\0 x10\0x11\0x12\0x13\0x14\0x15\0"
                     "x16\0x17\0x18\0x19\0x20\0x21\0x22\0x23\0"
                     "x24\0x25\0x26\0x27\0x28\0x29\0x30\0sp\0";
  unsigned len = idx >= 10 && !(idx == 31 && sf) ? 3 : 2;
  return da_strpcat4(dst, (sf ? xstr : wstr) + idx * 4, len);
}

static char* da_strpcatregv(char* restrict dst, unsigned idx) {
  const char* vstr = "v0\0 v1\0 v2\0 v3\0 v4\0 v5\0 v6\0 v7\0 "
                     "v8\0 v9\0 v10\0v11\0v12\0v13\0v14\0v15\0"
                     "v16\0v17\0v18\0v19\0v20\0v21\0v22\0v23\0"
                     "v24\0v25\0v26\0v27\0v28\0v29\0v30\0v31";
  return da_strpcat4(dst, vstr + 4 * idx, idx >= 10 ? 3 : 2);
}

static VOID da64_format(const struct Da64Inst* ddi, char* buf128) {
  da64_format_abs(ddi, 0, buf128);
}

static VOID da64_format_abs(const struct Da64Inst* ddi, ULONGLONG addr, char* buf128) {
  if (ddi->mnem == DA64I_UNKNOWN) {
    *buf128 = '\0';
    return;
  }

  const char* const mnemstr =
#define DA64_DECSTR
#include "arm64-disarm-private.inc"
#undef DA64_DECSTR
      "\0\0\0\0\0\0\0\0\0\0\0\0"; // extra padding to avoid out-of-bounds copies
  static const USHORT mnemtab[] = {
#define DA64_DECSTRTAB
#include "arm64-disarm-private.inc"
#undef DA64_DECSTRTAB
  };
  const char* va = ".8b .16b.4h .8h .2s .4s .1d .2d .2h .1q";

  char* end = buf128;
  if (ddi->mnem < sizeof(mnemtab) / sizeof(mnemtab[0]))
    end = da_strpcat12(end, mnemstr + (mnemtab[ddi->mnem] & 0xfff),
                       mnemtab[ddi->mnem] >> 12);
  for (unsigned i = 0; i < sizeof(ddi->ops) / sizeof(ddi->ops[0]); i++) {
    if (ddi->ops[i].type == DA_OP_NONE)
      break;
    if (i && ddi->ops[0].type != DA_OP_COND)
      *(end++) = ',', *(end++) = ' ';
    else if (i || ddi->ops[0].type != DA_OP_COND)
      *(end++) = ' ';
    switch (ddi->ops[i].type) {
    case DA_OP_REGGP:
      end = da_strpcatreggp(end, ddi->ops[i].reggp.sf, ddi->ops[i].reg);
      break;
    case DA_OP_REGGPINC:
      end = da_strpcatreggp(end, ddi->ops[i].reggp.sf, ddi->ops[i].reg);
      *end++ = '!';
      break;
    case DA_OP_REGGPEXT: {
      end = da_strpcatreggp(end, ddi->ops[i].reggpext.sf, ddi->ops[i].reg);
      static const char* exttbl = ", uxtb\0, uxth\0, uxtw\0, uxtx\0"
                                  ", sxtb\0, sxth\0, sxtw\0, sxtx\0"
                                  ", lsl\0 , lsr\0 , asr\0 , ror\0  ";
      end = da_strpcat8(end, exttbl + ddi->ops[i].reggpext.ext * 7,
                        5 + (ddi->ops[i].reggpext.ext < 8));
      end = da_strpcatimmdecstr(end, ddi->ops[i].reggpext.shift, 0);
      break;
    }
    case DA_OP_REGSP:
      end = da_strpcat4(end, &"wsp "[ddi->ops[i].reggp.sf],
                        3 - ddi->ops[i].reggp.sf);
      break;
    case DA_OP_REGFP: {
      const char* rstr = "b0\0 b1\0 b2\0 b3\0 b4\0 b5\0 b6\0 b7\0 "
                         "b8\0 b9\0 b10\0b11\0b12\0b13\0b14\0b15\0"
                         "b16\0b17\0b18\0b19\0b20\0b21\0b22\0b23\0"
                         "b24\0b25\0b26\0b27\0b28\0b29\0b30\0b31\0"
                         "h0\0 h1\0 h2\0 h3\0 h4\0 h5\0 h6\0 h7\0 "
                         "h8\0 h9\0 h10\0h11\0h12\0h13\0h14\0h15\0"
                         "h16\0h17\0h18\0h19\0h20\0h21\0h22\0h23\0"
                         "h24\0h25\0h26\0h27\0h28\0h29\0h30\0h31\0"
                         "s0\0 s1\0 s2\0 s3\0 s4\0 s5\0 s6\0 s7\0 "
                         "s8\0 s9\0 s10\0s11\0s12\0s13\0s14\0s15\0"
                         "s16\0s17\0s18\0s19\0s20\0s21\0s22\0s23\0"
                         "s24\0s25\0s26\0s27\0s28\0s29\0s30\0s31\0"
                         "d0\0 d1\0 d2\0 d3\0 d4\0 d5\0 d6\0 d7\0 "
                         "d8\0 d9\0 d10\0d11\0d12\0d13\0d14\0d15\0"
                         "d16\0d17\0d18\0d19\0d20\0d21\0d22\0d23\0"
                         "d24\0d25\0d26\0d27\0d28\0d29\0d30\0d31\0"
                         "q0\0 q1\0 q2\0 q3\0 q4\0 q5\0 q6\0 q7\0 "
                         "q8\0 q9\0 q10\0q11\0q12\0q13\0q14\0q15\0"
                         "q16\0q17\0q18\0q19\0q20\0q21\0q22\0q23\0"
                         "q24\0q25\0q26\0q27\0q28\0q29\0q30\0q31";
      unsigned idx = ddi->ops[i].reg;
      unsigned sz = ddi->ops[i].regfp.size;
      end = da_strpcat4(end, rstr + (sz * 32 + idx) * 4, idx >= 10 ? 3 : 2);
      break;
    }
    case DA_OP_REGVEC:
      end = da_strpcatregv(end, ddi->ops[i].reg);
      end = da_strpcat4(end, va + 4 * ddi->ops[i].regvec.va,
                        ddi->ops[i].regvec.va == 1 ? 4 : 3);
      break;
    case DA_OP_REGVIDX:
      end = da_strpcatregv(end, ddi->ops[i].reg);
      end = da_strpcat4(
          end,
          &".b[\0.h[\0.s[\0.d[\0.q[\0.2b[.4b[.2h["[4 *
                                                   ddi->ops[i].regvidx.esize],
          3 + (ddi->ops[i].regvidx.esize > 4));
      end = da_strpcatimmdecstr(end, ddi->ops[i].regvidx.elem, 2);
      *(end++) = ']';
      break;
    case DA_OP_REGVTBL:
      *(end++) = '{';
      end = da_strpcatregv(end, ddi->ops[i].reg);
      end = da_strpcat4(end, va + 4 * ddi->ops[i].regvtbl.va,
                        ddi->ops[i].regvtbl.va == 1 ? 4 : 3);
      if (ddi->ops[i].regvtbl.cnt > 1) {
        *(end++) = '-';
        end = da_strpcatregv(
            end, (ddi->ops[i].reg + ddi->ops[i].regvtbl.cnt - 1) & 31);
        end = da_strpcat4(end, va + 4 * ddi->ops[i].regvtbl.va,
                          ddi->ops[i].regvtbl.va == 1 ? 4 : 3);
      }
      *(end++) = '}';
      break;
    case DA_OP_REGVTBLIDX:
      *(end++) = '{';
      end = da_strpcatregv(end, ddi->ops[i].reg);
      end = da_strpcat4(end, &".b.h.s.d "[ddi->ops[i].regvtblidx.esize * 2], 2);
      if (ddi->ops[i].regvtbl.cnt > 1) {
        *(end++) = '-';
        end = da_strpcatregv(
            end, (ddi->ops[i].reg + ddi->ops[i].regvtblidx.cnt - 1) & 31);
        end =
            da_strpcat4(end, &".b.h.s.d "[ddi->ops[i].regvtblidx.esize * 2], 2);
      }
      end = da_strpcat4(end, " }[", 3);
      end = da_strpcatimmdecstr(end, ddi->ops[i].regvtblidx.elem, 2);
      *(end++) = ']';
      break;
    case DA_OP_MEMUOFF:
      *(end++) = '[';
      end = da_strpcatreggpsp(end, 1, ddi->ops[i].reg);
      if (ddi->ops[i].uimm16) {
        end = da_strpcat8(end, ", #0x   ", 5);
        end = da_strpcatuimmhex(end, ddi->ops[i].uimm16);
      }
      *(end++) = ']';
      break;
    case DA_OP_MEMSOFF:
      *(end++) = '[';
      end = da_strpcatreggpsp(end, 1, ddi->ops[i].reg);
      if (ddi->ops[i].simm16)
        end = da_strpcatsimmhex16(end, ddi->ops[i].simm16, 1);
      *(end++) = ']';
      break;
    case DA_OP_MEMSOFFPRE:
      *(end++) = '[';
      end = da_strpcatreggpsp(end, 1, ddi->ops[i].reg);
      end = da_strpcatsimmhex16(end, ddi->ops[i].simm16, 1);
      end = da_strpcat4(end, "]! ", 2);
      break;
    case DA_OP_MEMSOFFPOST:
      *(end++) = '[';
      end = da_strpcatreggpsp(end, 1, ddi->ops[i].reg);
      end = da_strpcatsimmhex16(end, ddi->ops[i].simm16, 0);
      break;
    case DA_OP_MEMREG: {
      *(end++) = '[';
      end = da_strpcatreggpsp(end, 1, ddi->ops[i].reg);
      end = da_strpcat4(end, ",  ", 2);
      end = da_strpcatreggp(end, (ddi->ops[i].memreg.ext & 3) == 3,
                            ddi->ops[i].memreg.offreg);
      // compact option: 2 => 0; 3 => 1; 6 => 2; 7 => 4
      unsigned optidx = (ddi->ops[i].memreg.ext - 1) >> 1;
      if (!ddi->ops[i].memreg.sc) {
        const char* ext = ", uxtw]\0]\0      , sxtw]\0, sxtx]";
        end = da_strpcat8(end, ext + 8 * optidx, optidx == 1 ? 1 : 7);
      } else {
        const char* ext = ", uxtw #0]\0, lsl #0]\0 , sxtw #0]\0, sxtx #0]\0"
                          ", uxtw #1]\0, lsl #1]\0 , sxtw #1]\0, sxtx #1]\0"
                          ", uxtw #2]\0, lsl #2]\0 , sxtw #2]\0, sxtx #2]\0"
                          ", uxtw #3]\0, lsl #3]\0 , sxtw #3]\0, sxtx #3]\0"
                          ", uxtw #4]\0, lsl #4]\0 , sxtw #4]\0, sxtx #4]\0";
        end = da_strpcat12(
            end, ext + 11 * (optidx + 4 * ddi->ops[i].memreg.shift), 10);
      }
      break;
    }
    case DA_OP_MEMREGPOST:
      *(end++) = '[';
      end = da_strpcatreggpsp(end, 1, ddi->ops[i].reg);
      end = da_strpcat4(end, "], ", 3);
      end = da_strpcatreggp(end, 1, ddi->ops[i].memreg.offreg);
      break;
    case DA_OP_MEMINC:
      *(end++) = '[';
      end = da_strpcatreggp(end, 1, ddi->ops[i].reg);
      end = da_strpcat4(end, "]! ", 2);
      break;
    case DA_OP_IMMSMALL:
      end = da_strpcatimmdecstr(end, ddi->ops[i].uimm16, 1);
      break;
    case DA_OP_IMMLARGE:
      end = da_strpcat4(end, "#0x", 3);
      end = da_strpcatuimmhex(end, ddi->imm64);
      break;
    case DA_OP_RELADDR:
      end = da_strpcat4(end, "#0x", 3);
      end = da_strpcatuimmhex(end, ddi->imm64 + addr);
      break;
    case DA_OP_SIMM:
      end = da_strpcatsimmhex16(end, ddi->ops[i].simm16, 3);
      break;
    case DA_OP_UIMM:
      end = da_strpcat4(end, "#0x", 3);
      end = da_strpcatuimmhex(end, ddi->ops[i].uimm16);
      break;
    case DA_OP_UIMMSHIFT:
      end = da_strpcat4(end, "#0x", 3);
      end = da_strpcatuimmhex(end, ddi->ops[i].uimm16);
      end = da_strpcat8(end, &", lsl, msl  "[ddi->ops[i].immshift.mask * 5], 5);
      end = da_strpcatimmdecstr(end, ddi->ops[i].immshift.shift, 0);
      break;
    case DA_OP_IMMFLOAT: {
      const char* floattable =
          "\x02#2         \x06#2.125     \x05#2.25      \x06#2.375     "
          "\x04#2.5       \x06#2.625     \x05#2.75      \x06#2.875     "
          "\x02#3         \x06#3.125     \x05#3.25      \x06#3.375     "
          "\x04#3.5       \x06#3.625     \x05#3.75      \x06#3.875     "
          "\x02#4         \x05#4.25      \x04#4.5       \x05#4.75      "
          "\x02#5         \x05#5.25      \x04#5.5       \x05#5.75      "
          "\x02#6         \x05#6.25      \x04#6.5       \x05#6.75      "
          "\x02#7         \x05#7.25      \x04#7.5       \x05#7.75      "
          "\x02#8         \x04#8.5       \x02#9         \x04#9.5       "
          "\x03#10        \x05#10.5      \x03#11        \x05#11.5      "
          "\x03#12        \x05#12.5      \x03#13        \x05#13.5      "
          "\x03#14        \x05#14.5      \x03#15        \x05#15.5      "
          "\x03#16        \x03#17        \x03#18        \x03#19        "
          "\x03#20        \x03#21        \x03#22        \x03#23        "
          "\x03#24        \x03#25        \x03#26        \x03#27        "
          "\x03#28        \x03#29        \x03#30        \x03#31        "
          "\x06#0.125     \x0a#0.1328125 \x09#0.140625  \x0a#0.1484375 "
          "\x08#0.15625   \x0a#0.1640625 \x09#0.171875  \x0a#0.1796875 "
          "\x07#0.1875    \x0a#0.1953125 \x09#0.203125  \x0a#0.2109375 "
          "\x08#0.21875   \x0a#0.2265625 \x09#0.234375  \x0a#0.2421875 "
          "\x05#0.25      \x09#0.265625  \x08#0.28125   \x09#0.296875  "
          "\x07#0.3125    \x09#0.328125  \x08#0.34375   \x09#0.359375  "
          "\x06#0.375     \x09#0.390625  \x08#0.40625   \x09#0.421875  "
          "\x07#0.4375    \x09#0.453125  \x08#0.46875   \x09#0.484375  "
          "\x04#0.5       \x08#0.53125   \x07#0.5625    \x08#0.59375   "
          "\x06#0.625     \x08#0.65625   \x07#0.6875    \x08#0.71875   "
          "\x05#0.75      \x08#0.78125   \x07#0.8125    \x08#0.84375   "
          "\x06#0.875     \x08#0.90625   \x07#0.9375    \x08#0.96875   "
          "\x02#1         \x07#1.0625    \x06#1.125     \x07#1.1875    "
          "\x05#1.25      \x07#1.3125    \x06#1.375     \x07#1.4375    "
          "\x04#1.5       \x07#1.5625    \x06#1.625     \x07#1.6875    "
          "\x05#1.75      \x07#1.8125    \x06#1.875     \x07#1.9375    "
          "\x03#-2        \x07#-2.125    \x06#-2.25     \x07#-2.375    "
          "\x05#-2.5      \x07#-2.625    \x06#-2.75     \x07#-2.875    "
          "\x03#-3        \x07#-3.125    \x06#-3.25     \x07#-3.375    "
          "\x05#-3.5      \x07#-3.625    \x06#-3.75     \x07#-3.875    "
          "\x03#-4        \x06#-4.25     \x05#-4.5      \x06#-4.75     "
          "\x03#-5        \x06#-5.25     \x05#-5.5      \x06#-5.75     "
          "\x03#-6        \x06#-6.25     \x05#-6.5      \x06#-6.75     "
          "\x03#-7        \x06#-7.25     \x05#-7.5      \x06#-7.75     "
          "\x03#-8        \x05#-8.5      \x03#-9        \x05#-9.5      "
          "\x04#-10       \x06#-10.5     \x04#-11       \x06#-11.5     "
          "\x04#-12       \x06#-12.5     \x04#-13       \x06#-13.5     "
          "\x04#-14       \x06#-14.5     \x04#-15       \x06#-15.5     "
          "\x04#-16       \x04#-17       \x04#-18       \x04#-19       "
          "\x04#-20       \x04#-21       \x04#-22       \x04#-23       "
          "\x04#-24       \x04#-25       \x04#-26       \x04#-27       "
          "\x04#-28       \x04#-29       \x04#-30       \x04#-31       "
          "\x07#-0.125    \x0b#-0.1328125\x0a#-0.140625 \x0b#-0.1484375"
          "\x09#-0.15625  \x0b#-0.1640625\x0a#-0.171875 \x0b#-0.1796875"
          "\x08#-0.1875   \x0b#-0.1953125\x0a#-0.203125 \x0b#-0.2109375"
          "\x09#-0.21875  \x0b#-0.2265625\x0a#-0.234375 \x0b#-0.2421875"
          "\x06#-0.25     \x0a#-0.265625 \x09#-0.28125  \x0a#-0.296875 "
          "\x08#-0.3125   \x0a#-0.328125 \x09#-0.34375  \x0a#-0.359375 "
          "\x07#-0.375    \x0a#-0.390625 \x09#-0.40625  \x0a#-0.421875 "
          "\x08#-0.4375   \x0a#-0.453125 \x09#-0.46875  \x0a#-0.484375 "
          "\x05#-0.5      \x09#-0.53125  \x08#-0.5625   \x09#-0.59375  "
          "\x07#-0.625    \x09#-0.65625  \x08#-0.6875   \x09#-0.71875  "
          "\x06#-0.75     \x09#-0.78125  \x08#-0.8125   \x09#-0.84375  "
          "\x07#-0.875    \x09#-0.90625  \x08#-0.9375   \x09#-0.96875  "
          "\x03#-1        \x08#-1.0625   \x07#-1.125    \x08#-1.1875   "
          "\x06#-1.25     \x08#-1.3125   \x07#-1.375    \x08#-1.4375   "
          "\x05#-1.5      \x08#-1.5625   \x07#-1.625    \x08#-1.6875   "
          "\x06#-1.75     \x08#-1.8125   \x07#-1.875    \x08#-1.9375   "
          "\x04#0.0       ";
      const char* floatelem = floattable + 12 * ddi->ops[i].uimm16;
      end = da_strpcat12(end, floatelem + 1, *floatelem);
      break;
    }
    case DA_OP_COND: {
      const char* cc = "eqnecsccmiplvsvchilsgeltgtlealnv ";
      end = da_strpcat4(end, cc + ddi->ops[i].cond * 2, 2);
      break;
    }
    case DA_OP_PRFOP: {
      const char* prfstr =
          "\x09pldl1keep\x09pldl1strm\x09pldl2keep\x09pldl2strm"
          "\x09pldl3keep\x09pldl3strm\x02#6       \x02#7       "
          "\x09plil1keep\x09plil1strm\x09plil2keep\x09plil2strm"
          "\x09plil3keep\x09plil3strm\x03#14      \x03#15      "
          "\x09pstl1keep\x09pstl1strm\x09pstl2keep\x09pstl2strm"
          "\x09pstl3keep\x09pstl3strm\x03#22      \x03#23      "
          "\x03#24      \x03#25      \x03#26      \x03#27      "
          "\x03#28      \x03#29      \x03#30      \x03#31      \0\0";
      const char* prfstrelem = prfstr + 10 * ddi->ops[i].prfop;
      end = da_strpcat12(end, prfstrelem + 1, *prfstrelem);
      break;
    }
    default: break;
    }
  }
  *end = 0;
}
