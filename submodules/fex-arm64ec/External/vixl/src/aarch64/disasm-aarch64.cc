// Copyright 2015, VIXL authors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of ARM Limited nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "disasm-aarch64.h"

#include <bitset>
#include <cstdlib>
#include <sstream>

namespace vixl {
namespace aarch64 {

void Disassembler::PopulateFormToStringMap(FormToStringMap *fts) {
  using StringToFormMap =
      std::unordered_map<std::string, std::unordered_set<uint32_t>>;

  // Map from disassembler format string to instruction form that uses it. On
  // object construction, this is used to build a map from instruction to
  // disassembler string, allowing fast lookup during disassembly.
  static const StringToFormMap forms = {
      {"",
       {"autia1716_hi_hints"_h, "autiasp_hi_hints"_h,   "autiaz_hi_hints"_h,
        "autib1716_hi_hints"_h, "autibsp_hi_hints"_h,   "autibz_hi_hints"_h,
        "axflag_m_pstate"_h,    "cfinv_m_pstate"_h,     "csdb_hi_hints"_h,
        "dgh_hi_hints"_h,       "ssbb_only_barriers"_h, "esb_hi_hints"_h,
        "isb_bi_barriers"_h,    "nop_hi_hints"_h,       "pacia1716_hi_hints"_h,
        "paciasp_hi_hints"_h,   "paciaz_hi_hints"_h,    "pacib1716_hi_hints"_h,
        "pacibsp_hi_hints"_h,   "pacibz_hi_hints"_h,    "setffr_f"_h,
        "sev_hi_hints"_h,       "sevl_hi_hints"_h,      "wfe_hi_hints"_h,
        "wfi_hi_hints"_h,       "xaflag_m_pstate"_h,    "xpaclri_hi_hints"_h,
        "yield_hi_hints"_h}},
      {"'?22:ds'u0400, '?22:ds'u0905",
       {"fcvtas_asisdmisc_r"_h,
        "fcvtau_asisdmisc_r"_h,
        "fcvtms_asisdmisc_r"_h,
        "fcvtmu_asisdmisc_r"_h,
        "fcvtns_asisdmisc_r"_h,
        "fcvtnu_asisdmisc_r"_h,
        "fcvtps_asisdmisc_r"_h,
        "fcvtpu_asisdmisc_r"_h,
        "fcvtzs_asisdmisc_r"_h,
        "fcvtzu_asisdmisc_r"_h,
        "frecpe_asisdmisc_r"_h,
        "frecpx_asisdmisc_r"_h,
        "frsqrte_asisdmisc_r"_h,
        "scvtf_asisdmisc_r"_h,
        "ucvtf_asisdmisc_r"_h}},
      {"'?22:ds'u0400, '?22:ds'u0905, #0.0",
       {"fcmeq_asisdmisc_fz"_h,
        "fcmge_asisdmisc_fz"_h,
        "fcmgt_asisdmisc_fz"_h,
        "fcmle_asisdmisc_fz"_h,
        "fcmlt_asisdmisc_fz"_h}},
      {"'?22:ds'u0400, '?22:ds'u0905, '?22:ds'u2016",
       {"fabd_asisdsame_only"_h,
        "facge_asisdsame_only"_h,
        "facgt_asisdsame_only"_h,
        "fcmeq_asisdsame_only"_h,
        "fcmge_asisdsame_only"_h,
        "fcmgt_asisdsame_only"_h,
        "fmulx_asisdsame_only"_h,
        "frecps_asisdsame_only"_h,
        "frsqrts_asisdsame_only"_h}},
      {"'?22:ds'u0400, 'Vn.2'?22:ds",
       {"faddp_asisdpair_only_sd"_h,
        "fmaxnmp_asisdpair_only_sd"_h,
        "fmaxp_asisdpair_only_sd"_h,
        "fminnmp_asisdpair_only_sd"_h,
        "fminp_asisdpair_only_sd"_h}},
      {"'?22:ds'u0400, '?22:ds'u0905, 'Vf.'?22:ds['IVByElemIndex]",
       {"fmla_asisdelem_r_sd"_h,
        "fmls_asisdelem_r_sd"_h,
        "fmul_asisdelem_r_sd"_h,
        "fmulx_asisdelem_r_sd"_h}},
      {"'Bt, ['Xns'ILS]", {"ldur_b_ldst_unscaled"_h, "stur_b_ldst_unscaled"_h}},
      {"'Dt, 'Dt2, ['Xns'ILP3]",
       {"ldnp_d_ldstnapair_offs"_h, "stnp_d_ldstnapair_offs"_h}},
      {"'Dt, 'ILLiteral 'LValue", {"ldr_d_loadlit"_h}},
      {"'Dt, ['Xns'ILS]", {"ldur_d_ldst_unscaled"_h, "stur_d_ldst_unscaled"_h}},
      {"'Fd, 'Fn, 'Fm",
       {"fadd_d_floatdp2"_h,   "fadd_h_floatdp2"_h,   "fadd_s_floatdp2"_h,
        "fdiv_d_floatdp2"_h,   "fdiv_h_floatdp2"_h,   "fdiv_s_floatdp2"_h,
        "fmax_d_floatdp2"_h,   "fmax_h_floatdp2"_h,   "fmax_s_floatdp2"_h,
        "fmaxnm_d_floatdp2"_h, "fmaxnm_h_floatdp2"_h, "fmaxnm_s_floatdp2"_h,
        "fmin_d_floatdp2"_h,   "fmin_h_floatdp2"_h,   "fmin_s_floatdp2"_h,
        "fminnm_d_floatdp2"_h, "fminnm_h_floatdp2"_h, "fminnm_s_floatdp2"_h,
        "fmul_d_floatdp2"_h,   "fmul_h_floatdp2"_h,   "fmul_s_floatdp2"_h,
        "fnmul_d_floatdp2"_h,  "fnmul_h_floatdp2"_h,  "fnmul_s_floatdp2"_h,
        "fsub_d_floatdp2"_h,   "fsub_h_floatdp2"_h,   "fsub_s_floatdp2"_h}},
      {"'Fd, 'Fn, 'Fm, 'Cond",
       {"fcsel_d_floatsel"_h, "fcsel_h_floatsel"_h, "fcsel_s_floatsel"_h}},
      {"'Fd, 'Fn, 'Fm, 'Fa",
       {"fmadd_d_floatdp3"_h,
        "fmadd_h_floatdp3"_h,
        "fmadd_s_floatdp3"_h,
        "fmsub_d_floatdp3"_h,
        "fmsub_h_floatdp3"_h,
        "fmsub_s_floatdp3"_h,
        "fnmadd_d_floatdp3"_h,
        "fnmadd_h_floatdp3"_h,
        "fnmadd_s_floatdp3"_h,
        "fnmsub_d_floatdp3"_h,
        "fnmsub_h_floatdp3"_h,
        "fnmsub_s_floatdp3"_h}},
      {"'Fd, 'Rn",
       {"fmov_d64_float2int"_h,
        "fmov_h32_float2int"_h,
        "fmov_h64_float2int"_h,
        "fmov_s32_float2int"_h,
        "scvtf_d32_float2int"_h,
        "scvtf_d64_float2int"_h,
        "scvtf_h32_float2int"_h,
        "scvtf_h64_float2int"_h,
        "scvtf_s32_float2int"_h,
        "scvtf_s64_float2int"_h,
        "ucvtf_d32_float2int"_h,
        "ucvtf_d64_float2int"_h,
        "ucvtf_h32_float2int"_h,
        "ucvtf_h64_float2int"_h,
        "ucvtf_s32_float2int"_h,
        "ucvtf_s64_float2int"_h}},
      {"'Fd, 'Rn, 'IFPFBits",
       {"scvtf_d32_float2fix"_h,
        "scvtf_d64_float2fix"_h,
        "scvtf_h32_float2fix"_h,
        "scvtf_h64_float2fix"_h,
        "scvtf_s32_float2fix"_h,
        "scvtf_s64_float2fix"_h,
        "ucvtf_d32_float2fix"_h,
        "ucvtf_d64_float2fix"_h,
        "ucvtf_h32_float2fix"_h,
        "ucvtf_h64_float2fix"_h,
        "ucvtf_s32_float2fix"_h,
        "ucvtf_s64_float2fix"_h}},
      {"'Fn, #0.0",
       {"fcmp_dz_floatcmp"_h,
        "fcmp_hz_floatcmp"_h,
        "fcmp_sz_floatcmp"_h,
        "fcmpe_dz_floatcmp"_h,
        "fcmpe_hz_floatcmp"_h,
        "fcmpe_sz_floatcmp"_h}},
      {"'Fn, 'Fm",
       {"fcmp_d_floatcmp"_h,
        "fcmp_h_floatcmp"_h,
        "fcmp_s_floatcmp"_h,
        "fcmpe_d_floatcmp"_h,
        "fcmpe_h_floatcmp"_h,
        "fcmpe_s_floatcmp"_h}},
      {"'Fn, 'Fm, 'INzcv, 'Cond",
       {"fccmp_d_floatccmp"_h,
        "fccmp_h_floatccmp"_h,
        "fccmp_s_floatccmp"_h,
        "fccmpe_d_floatccmp"_h,
        "fccmpe_h_floatccmp"_h,
        "fccmpe_s_floatccmp"_h}},
      {"'Hd, 'Hn",
       {"fcvtas_asisdmiscfp16_r"_h,
        "fcvtau_asisdmiscfp16_r"_h,
        "fcvtms_asisdmiscfp16_r"_h,
        "fcvtmu_asisdmiscfp16_r"_h,
        "fcvtns_asisdmiscfp16_r"_h,
        "fcvtnu_asisdmiscfp16_r"_h,
        "fcvtps_asisdmiscfp16_r"_h,
        "fcvtpu_asisdmiscfp16_r"_h,
        "fcvtzs_asisdmiscfp16_r"_h,
        "fcvtzu_asisdmiscfp16_r"_h,
        "frecpe_asisdmiscfp16_r"_h,
        "frecpx_asisdmiscfp16_r"_h,
        "frsqrte_asisdmiscfp16_r"_h,
        "scvtf_asisdmiscfp16_r"_h,
        "ucvtf_asisdmiscfp16_r"_h}},
      {"'Hd, 'Hn, #0.0",
       {"fcmeq_asisdmiscfp16_fz"_h,
        "fcmge_asisdmiscfp16_fz"_h,
        "fcmgt_asisdmiscfp16_fz"_h,
        "fcmle_asisdmiscfp16_fz"_h,
        "fcmlt_asisdmiscfp16_fz"_h}},
      {"'Hd, 'Hn, 'Hm",
       {"fabd_asisdsamefp16_only"_h,
        "facge_asisdsamefp16_only"_h,
        "facgt_asisdsamefp16_only"_h,
        "fcmeq_asisdsamefp16_only"_h,
        "fcmge_asisdsamefp16_only"_h,
        "fcmgt_asisdsamefp16_only"_h,
        "fmulx_asisdsamefp16_only"_h,
        "frecps_asisdsamefp16_only"_h,
        "frsqrts_asisdsamefp16_only"_h}},
      {"'Hd, 'Vn.'?30:84h",
       {"fmaxnmv_asimdall_only_h"_h,
        "fmaxv_asimdall_only_h"_h,
        "fminnmv_asimdall_only_h"_h,
        "fminv_asimdall_only_h"_h}},
      {"'Hd, 'Vn.2h",
       {"faddp_asisdpair_only_h"_h,
        "fmaxnmp_asisdpair_only_h"_h,
        "fmaxp_asisdpair_only_h"_h,
        "fminnmp_asisdpair_only_h"_h,
        "fminp_asisdpair_only_h"_h}},
      {"'Ht, ['Xns'ILS]", {"ldur_h_ldst_unscaled"_h, "stur_h_ldst_unscaled"_h}},
      {"'IDebug",
       {"brk_ex_exception"_h,
        "hlt_ex_exception"_h,
        "hvc_ex_exception"_h,
        "smc_ex_exception"_h,
        "svc_ex_exception"_h}},
      {"'Pd.'t, 'Pgl/z, 'Zn.'t, #'s2016",
       {"cmpeq_p_p_zi"_h,
        "cmpge_p_p_zi"_h,
        "cmpgt_p_p_zi"_h,
        "cmple_p_p_zi"_h,
        "cmplt_p_p_zi"_h,
        "cmpne_p_p_zi"_h}},
      {"'Pd.'t, 'Pgl/z, 'Zn.'t, #'u2014",
       {"cmphi_p_p_zi"_h,
        "cmphs_p_p_zi"_h,
        "cmplo_p_p_zi"_h,
        "cmpls_p_p_zi"_h}},
      {"'Pd.'t, 'Pgl/z, 'Zn.'t, 'Zm.'t",
       {"cmpeq_p_p_zz"_h,
        "cmpge_p_p_zz"_h,
        "cmpgt_p_p_zz"_h,
        "cmphi_p_p_zz"_h,
        "cmphs_p_p_zz"_h,
        "cmpne_p_p_zz"_h}},
      {"'Pd.'t, 'Pgl/z, 'Zn.'t, 'Zm.d",
       {"cmpeq_p_p_zw"_h,
        "cmpge_p_p_zw"_h,
        "cmpgt_p_p_zw"_h,
        "cmphi_p_p_zw"_h,
        "cmphs_p_p_zw"_h,
        "cmple_p_p_zw"_h,
        "cmplo_p_p_zw"_h,
        "cmpls_p_p_zw"_h,
        "cmplt_p_p_zw"_h,
        "cmpne_p_p_zw"_h}},
      {"'Pd.'t, 'Pn, 'Pd.'t", {"pnext_p_p_p"_h}},
      {"'Pd.'t, 'Pn.'t, 'Pm.'t",
       {"trn1_p_pp"_h,
        "trn2_p_pp"_h,
        "uzp1_p_pp"_h,
        "uzp2_p_pp"_h,
        "zip1_p_pp"_h,
        "zip2_p_pp"_h}},
      {"'Pd.b", {"pfalse_p"_h, "rdffr_p_f"_h}},
      {"'Pd.b, 'Pn, 'Pd.b", {"pfirst_p_p_p"_h}},
      {"'Pd.b, 'Pn/z", {"rdffrs_p_p_f"_h, "rdffr_p_p_f"_h}},
      {"'Pd.b, p'u1310/'?04:mz, 'Pn.b",
       {"brka_p_p_p"_h, "brkas_p_p_p_z"_h, "brkb_p_p_p"_h, "brkbs_p_p_p_z"_h}},
      {"'Pd.b, p'u1310/z, 'Pn.b, 'Pd.b", {"brkn_p_p_pp"_h, "brkns_p_p_pp"_h}},
      {"'Pd.b, p'u1310/z, 'Pn.b, 'Pm.b",
       {"brkpas_p_p_pp"_h,
        "brkpa_p_p_pp"_h,
        "brkpbs_p_p_pp"_h,
        "brkpb_p_p_pp"_h}},
      {"'Pd.h, 'Pn.b", {"punpkhi_p_p"_h, "punpklo_p_p"_h}},
      {"'Pn.b", {"wrffr_f_p"_h}},
      {"'Qd, 'Qn, 'Vm.2d",
       {"sha512h2_qqv_cryptosha512_3"_h, "sha512h_qqv_cryptosha512_3"_h}},
      {"'Qd, 'Qn, 'Vm.4s",
       {"sha256h2_qqv_cryptosha3"_h, "sha256h_qqv_cryptosha3"_h}},
      {"'Qd, 'Sn, 'Vm.4s",
       {"sha1c_qsv_cryptosha3"_h,
        "sha1m_qsv_cryptosha3"_h,
        "sha1p_qsv_cryptosha3"_h}},
      {"'Qt, 'ILLiteral 'LValue", {"ldr_q_loadlit"_h}},
      {"'Qt, 'Qt2, ['Xns'ILP4]",
       {"ldnp_q_ldstnapair_offs"_h, "stnp_q_ldstnapair_offs"_h}},
      {"'Qt, ['Xns'ILS]", {"ldur_q_ldst_unscaled"_h, "stur_q_ldst_unscaled"_h}},
      {"'Rd, 'Fn", {"fcvtas_32d_float2int"_h,  "fcvtas_32h_float2int"_h,
                    "fcvtas_32s_float2int"_h,  "fcvtas_64d_float2int"_h,
                    "fcvtas_64h_float2int"_h,  "fcvtas_64s_float2int"_h,
                    "fcvtau_32d_float2int"_h,  "fcvtau_32h_float2int"_h,
                    "fcvtau_32s_float2int"_h,  "fcvtau_64d_float2int"_h,
                    "fcvtau_64h_float2int"_h,  "fcvtau_64s_float2int"_h,
                    "fcvtms_32d_float2int"_h,  "fcvtms_32h_float2int"_h,
                    "fcvtms_32s_float2int"_h,  "fcvtms_64d_float2int"_h,
                    "fcvtms_64h_float2int"_h,  "fcvtms_64s_float2int"_h,
                    "fcvtmu_32d_float2int"_h,  "fcvtmu_32h_float2int"_h,
                    "fcvtmu_32s_float2int"_h,  "fcvtmu_64d_float2int"_h,
                    "fcvtmu_64h_float2int"_h,  "fcvtmu_64s_float2int"_h,
                    "fcvtns_32d_float2int"_h,  "fcvtns_32h_float2int"_h,
                    "fcvtns_32s_float2int"_h,  "fcvtns_64d_float2int"_h,
                    "fcvtns_64h_float2int"_h,  "fcvtns_64s_float2int"_h,
                    "fcvtnu_32d_float2int"_h,  "fcvtnu_32h_float2int"_h,
                    "fcvtnu_32s_float2int"_h,  "fcvtnu_64d_float2int"_h,
                    "fcvtnu_64h_float2int"_h,  "fcvtnu_64s_float2int"_h,
                    "fcvtps_32d_float2int"_h,  "fcvtps_32h_float2int"_h,
                    "fcvtps_32s_float2int"_h,  "fcvtps_64d_float2int"_h,
                    "fcvtps_64h_float2int"_h,  "fcvtps_64s_float2int"_h,
                    "fcvtpu_32d_float2int"_h,  "fcvtpu_32h_float2int"_h,
                    "fcvtpu_32s_float2int"_h,  "fcvtpu_64d_float2int"_h,
                    "fcvtpu_64h_float2int"_h,  "fcvtpu_64s_float2int"_h,
                    "fcvtzs_32d_float2int"_h,  "fcvtzs_32h_float2int"_h,
                    "fcvtzs_32s_float2int"_h,  "fcvtzs_64d_float2int"_h,
                    "fcvtzs_64h_float2int"_h,  "fcvtzs_64s_float2int"_h,
                    "fcvtzu_32d_float2int"_h,  "fcvtzu_32h_float2int"_h,
                    "fcvtzu_32s_float2int"_h,  "fcvtzu_64d_float2int"_h,
                    "fcvtzu_64h_float2int"_h,  "fcvtzu_64s_float2int"_h,
                    "fjcvtzs_32d_float2int"_h, "fmov_32h_float2int"_h,
                    "fmov_32s_float2int"_h,    "fmov_64d_float2int"_h,
                    "fmov_64h_float2int"_h}},
      {"'Rd, 'Fn, 'IFPFBits",
       {"fcvtzs_32d_float2fix"_h,
        "fcvtzs_32h_float2fix"_h,
        "fcvtzs_32s_float2fix"_h,
        "fcvtzs_64d_float2fix"_h,
        "fcvtzs_64h_float2fix"_h,
        "fcvtzs_64s_float2fix"_h,
        "fcvtzu_32d_float2fix"_h,
        "fcvtzu_32h_float2fix"_h,
        "fcvtzu_32s_float2fix"_h,
        "fcvtzu_64d_float2fix"_h,
        "fcvtzu_64h_float2fix"_h,
        "fcvtzu_64s_float2fix"_h}},
      {"'Rd, 'Rn",
       {"abs_32_dp_1src"_h,
        "abs_64_dp_1src"_h,
        "cls_32_dp_1src"_h,
        "cls_64_dp_1src"_h,
        "clz_32_dp_1src"_h,
        "clz_64_dp_1src"_h,
        "cnt_32_dp_1src"_h,
        "cnt_64_dp_1src"_h,
        "ctz_32_dp_1src"_h,
        "ctz_64_dp_1src"_h,
        "rbit_32_dp_1src"_h,
        "rbit_64_dp_1src"_h,
        "rev16_32_dp_1src"_h,
        "rev16_64_dp_1src"_h,
        "rev32_64_dp_1src"_h,
        "rev_32_dp_1src"_h,
        "rev_64_dp_1src"_h}},
      {"'Rd, 'Rn, #'s1710",
       {"smax_32_minmax_imm"_h,
        "smax_64_minmax_imm"_h,
        "smin_32_minmax_imm"_h,
        "smin_64_minmax_imm"_h}},
      {"'Rd, 'Rn, #'u1710",
       {"umax_32u_minmax_imm"_h,
        "umax_64u_minmax_imm"_h,
        "umin_32u_minmax_imm"_h,
        "umin_64u_minmax_imm"_h}},
      {"'Rd, 'Rn, 'Rm",
       {"crc32b_32c_dp_2src"_h,
        "crc32cb_32c_dp_2src"_h,
        "crc32ch_32c_dp_2src"_h,
        "crc32cw_32c_dp_2src"_h,
        "crc32h_32c_dp_2src"_h,
        "crc32w_32c_dp_2src"_h,
        "sdiv_32_dp_2src"_h,
        "sdiv_64_dp_2src"_h,
        "smax_32_dp_2src"_h,
        "smax_64_dp_2src"_h,
        "smin_32_dp_2src"_h,
        "smin_64_dp_2src"_h,
        "udiv_32_dp_2src"_h,
        "udiv_64_dp_2src"_h,
        "umax_32_dp_2src"_h,
        "umax_64_dp_2src"_h,
        "umin_32_dp_2src"_h,
        "umin_64_dp_2src"_h}},
      {"'Rd, 'Vn.D[1]", {"fmov_64vx_float2int"_h}},
      {"'Rd, 'Xns, 'Rm", {"gmi_64g_dp_2src"_h}},
      {"'Rn, 'IP, 'INzcv, 'Cond",
       {"ccmn_32_condcmp_imm"_h,
        "ccmn_64_condcmp_imm"_h,
        "ccmp_32_condcmp_imm"_h,
        "ccmp_64_condcmp_imm"_h}},
      {"'Rn, 'Rm, 'INzcv, 'Cond",
       {"ccmn_32_condcmp_reg"_h,
        "ccmn_64_condcmp_reg"_h,
        "ccmp_32_condcmp_reg"_h,
        "ccmp_64_condcmp_reg"_h}},
      {"'Rt, 'It, 'TImmTest",
       {"tbnz_only_testbranch"_h, "tbz_only_testbranch"_h}},
      {"'Rt, 'TImmCmpa",
       {"cbnz_32_compbranch"_h,
        "cbnz_64_compbranch"_h,
        "cbz_32_compbranch"_h,
        "cbz_64_compbranch"_h}},
      {"'Sd, 'Sn", {"sha1h_ss_cryptosha2"_h}},
      {"'St, 'ILLiteral 'LValue", {"ldr_s_loadlit"_h}},
      {"'St, 'St2, ['Xns'ILP2]",
       {"ldnp_s_ldstnapair_offs"_h, "stnp_s_ldstnapair_offs"_h}},
      {"'St, ['Xns'ILS]", {"ldur_s_ldst_unscaled"_h, "stur_s_ldst_unscaled"_h}},
      {"'TImmUncn", {"b_only_branch_imm"_h, "bl_only_branch_imm"_h}},
      {"'Vd.'?30:42s, 'Vn.'?30:42h, 'Ve.h['IVByElemIndexFHM]",
       {"fmlal2_asimdelem_lh"_h,
        "fmlal_asimdelem_lh"_h,
        "fmlsl2_asimdelem_lh"_h,
        "fmlsl_asimdelem_lh"_h}},
      {"'Vd.'?30:42s, 'Vn.'?30:42h, 'Vm.'?30:42h",
       {"fmlal2_asimdsame_f"_h,
        "fmlal_asimdsame_f"_h,
        "fmlsl2_asimdsame_f"_h,
        "fmlsl_asimdsame_f"_h}},
      {"'Vd.'?30:42s, 'Vn.'?30:84h, 'Vm.'?30:84h",
       {"bfdot_asimdsame2_d"_h, "bfmmla_asimdsame2_e"_h}},
      {"'Vd.'?30:42s, 'Vn.'?30:84h, 'Vm.2h['u1111:2121]",
       {"bfdot_asimdelem_e"_h}},
      {"'Vd.'?30:84h, 'Vn.'?30:84h",
       {"fabs_asimdmiscfp16_r"_h,    "fcvtas_asimdmiscfp16_r"_h,
        "fcvtau_asimdmiscfp16_r"_h,  "fcvtms_asimdmiscfp16_r"_h,
        "fcvtmu_asimdmiscfp16_r"_h,  "fcvtns_asimdmiscfp16_r"_h,
        "fcvtnu_asimdmiscfp16_r"_h,  "fcvtps_asimdmiscfp16_r"_h,
        "fcvtpu_asimdmiscfp16_r"_h,  "fcvtzs_asimdmiscfp16_r"_h,
        "fcvtzu_asimdmiscfp16_r"_h,  "fneg_asimdmiscfp16_r"_h,
        "frecpe_asimdmiscfp16_r"_h,  "frinta_asimdmiscfp16_r"_h,
        "frinti_asimdmiscfp16_r"_h,  "frintm_asimdmiscfp16_r"_h,
        "frintn_asimdmiscfp16_r"_h,  "frintp_asimdmiscfp16_r"_h,
        "frintx_asimdmiscfp16_r"_h,  "frintz_asimdmiscfp16_r"_h,
        "frsqrte_asimdmiscfp16_r"_h, "fsqrt_asimdmiscfp16_r"_h,
        "scvtf_asimdmiscfp16_r"_h,   "ucvtf_asimdmiscfp16_r"_h}},
      {"'Vd.'?30:84h, 'Vn.'?30:84h, #0.0",
       {"fcmeq_asimdmiscfp16_fz"_h,
        "fcmge_asimdmiscfp16_fz"_h,
        "fcmgt_asimdmiscfp16_fz"_h,
        "fcmle_asimdmiscfp16_fz"_h,
        "fcmlt_asimdmiscfp16_fz"_h}},
      {"'Vd.'?30:84h, 'Vn.'?30:84h, 'Ve.h['IVByElemIndex]",
       {"fmla_asimdelem_rh_h"_h,
        "fmls_asimdelem_rh_h"_h,
        "fmulx_asimdelem_rh_h"_h,
        "fmul_asimdelem_rh_h"_h}},
      {"'Vd.16b, 'Vn.16b",
       {"aesd_b_cryptoaes"_h,
        "aese_b_cryptoaes"_h,
        "aesimc_b_cryptoaes"_h,
        "aesmc_b_cryptoaes"_h}},
      {"'Vd.16b, 'Vn.16b, 'Vm.16b, 'Va.16b",
       {"bcax_vvv16_crypto4"_h, "eor3_vvv16_crypto4"_h}},
      {"'Vd.2d, 'Vn.2d", {"sha512su0_vv2_cryptosha512_2"_h}},
      {"'Vd.2d, 'Vn.2d, 'Vm.2d",
       {"rax1_vvv2_cryptosha512_3"_h, "sha512su1_vvv2_cryptosha512_3"_h}},
      {"'Vd.2d, 'Vn.2d, 'Vm.2d, #'u1510", {"xar_vvv2_crypto3_imm6"_h}},
      {"'Vd.4s, 'Vn.16b, 'Vm.16b",
       {"smmla_asimdsame2_g"_h,
        "ummla_asimdsame2_g"_h,
        "usmmla_asimdsame2_g"_h}},
      {"'Vd.4s, 'Vn.4s",
       {"sha1su1_vv_cryptosha2"_h,
        "sha256su0_vv_cryptosha2"_h,
        "sm4e_vv4_cryptosha512_2"_h}},
      {"'Vd.4s, 'Vn.4s, 'Vm.4s",
       {"sha1su0_vvv_cryptosha3"_h,
        "sha256su1_vvv_cryptosha3"_h,
        "sm3partw1_vvv4_cryptosha512_3"_h,
        "sm3partw2_vvv4_cryptosha512_3"_h,
        "sm4ekey_vvv4_cryptosha512_3"_h}},
      {"'Vd.4s, 'Vn.4s, 'Vm.4s, 'Va.4s", {"sm3ss1_vvv4_crypto4"_h}},
      {"'Vd.4s, 'Vn.4s, 'Vm.s['u1312]",
       {"sm3tt1a_vvv4_crypto3_imm2"_h,
        "sm3tt1b_vvv4_crypto3_imm2"_h,
        "sm3tt2a_vvv4_crypto3_imm2"_h,
        "sm3tt2b_vvv_crypto3_imm2"_h}},
      {"'Vd.D[1], 'Rn", {"fmov_v64i_float2int"_h}},
      {"'Wd, 'Pn.'t", {"uqdecp_r_p_r_uw"_h, "uqincp_r_p_r_uw"_h}},
      {"'Wd, 'Wn, 'Xm", {"crc32cx_64c_dp_2src"_h, "crc32x_64c_dp_2src"_h}},
      {"'Wn", {"setf16_only_setf"_h, "setf8_only_setf"_h}},
      {"'Wt, 'ILLiteral 'LValue", {"ldr_32_loadlit"_h}},
      {"'Wt, 'Wt2, ['Xns'ILP2]",
       {"ldnp_32_ldstnapair_offs"_h, "stnp_32_ldstnapair_offs"_h}},
      {"'Wt, ['Xns'ILS]",
       {"ldapur_32_ldapstl_unscaled"_h,
        "ldapurb_32_ldapstl_unscaled"_h,
        "ldapurh_32_ldapstl_unscaled"_h,
        "ldapursb_32_ldapstl_unscaled"_h,
        "ldapursh_32_ldapstl_unscaled"_h,
        "ldur_32_ldst_unscaled"_h,
        "ldurb_32_ldst_unscaled"_h,
        "ldurh_32_ldst_unscaled"_h,
        "ldursb_32_ldst_unscaled"_h,
        "ldursh_32_ldst_unscaled"_h,
        "stlur_32_ldapstl_unscaled"_h,
        "stlurb_32_ldapstl_unscaled"_h,
        "stlurh_32_ldapstl_unscaled"_h,
        "stur_32_ldst_unscaled"_h,
        "sturb_32_ldst_unscaled"_h,
        "sturh_32_ldst_unscaled"_h}},
      {"'Xd",
       {"autdza_64z_dp_1src"_h,
        "autdzb_64z_dp_1src"_h,
        "autiza_64z_dp_1src"_h,
        "autizb_64z_dp_1src"_h,
        "pacdza_64z_dp_1src"_h,
        "pacdzb_64z_dp_1src"_h,
        "paciza_64z_dp_1src"_h,
        "pacizb_64z_dp_1src"_h,
        "xpacd_64z_dp_1src"_h,
        "xpaci_64z_dp_1src"_h}},
      {"'Xd, #'s1005", {"rdvl_r_i"_h}},
      {"'Xd, 'AddrPCRelByte", {"adr_only_pcreladdr"_h}},
      {"'Xd, 'AddrPCRelPage", {"adrp_only_pcreladdr"_h}},
      {"'Xd, 'Pn.'t",
       {"decp_r_p_r"_h,
        "incp_r_p_r"_h,
        "sqdecp_r_p_r_x"_h,
        "sqincp_r_p_r_x"_h,
        "uqdecp_r_p_r_x"_h,
        "uqincp_r_p_r_x"_h}},
      {"'Xd, 'Pn.'t, 'Wd", {"sqdecp_r_p_r_sx"_h, "sqincp_r_p_r_sx"_h}},
      {"'Xd, 'Xn, 'Xms", {"pacga_64p_dp_2src"_h}},
      {"'Xd, 'Xns",
       {"autda_64p_dp_1src"_h,
        "autdb_64p_dp_1src"_h,
        "autia_64p_dp_1src"_h,
        "autib_64p_dp_1src"_h,
        "pacda_64p_dp_1src"_h,
        "pacdb_64p_dp_1src"_h,
        "pacia_64p_dp_1src"_h,
        "pacib_64p_dp_1src"_h}},
      {"'Xd, p'u1310, 'Pn.'t", {"cntp_r_p_p"_h}},
      {"'Xds, 'Xms, #'s1005", {"addpl_r_ri"_h, "addvl_r_ri"_h}},
      {"'Xn, 'IRr, 'INzcv", {"rmif_only_rmif"_h}},
      {"'Xt, 'ILLiteral 'LValue", {"ldr_64_loadlit"_h, "ldrsw_64_loadlit"_h}},
      {"'Xt, 'Xt2, ['Xns'ILP3]",
       {"ldnp_64_ldstnapair_offs"_h, "stnp_64_ldstnapair_offs"_h}},
      {"'Xt, ['Xns'ILA]!", {"ldraa_64w_ldst_pac"_h, "ldrab_64w_ldst_pac"_h}},
      {"'Xt, ['Xns'ILA]", {"ldraa_64_ldst_pac"_h, "ldrab_64_ldst_pac"_h}},
      {"'Xt, ['Xns'ILS]",
       {"ldapur_64_ldapstl_unscaled"_h,
        "ldapursb_64_ldapstl_unscaled"_h,
        "ldapursh_64_ldapstl_unscaled"_h,
        "ldapursw_64_ldapstl_unscaled"_h,
        "ldur_64_ldst_unscaled"_h,
        "ldursb_64_ldst_unscaled"_h,
        "ldursh_64_ldst_unscaled"_h,
        "ldursw_64_ldst_unscaled"_h,
        "stlur_64_ldapstl_unscaled"_h,
        "stur_64_ldst_unscaled"_h}},
      {"'Zd, 'Zn", {"movprfx_z_z"_h}},
      {"'Zd.'?22:ds, 'Zn.'?22:ds, 'Zm.'?22:ds",
       {"adclb_z_zzz"_h, "adclt_z_zzz"_h, "sbclb_z_zzz"_h, "sbclt_z_zzz"_h}},
      {"'Zd.'t, 'Pgl, 'Zd.'t, 'Zn.'t",
       {"clasta_z_p_zz"_h, "clastb_z_p_zz"_h, "splice_z_p_zz_des"_h}},
      {"'Zd.'t, 'Pgl, {'Zn.'t, 'Zn2.'t}", {"splice_z_p_zz_con"_h}},
      {"'Zd.'t, 'Pgl/'?16:mz, 'Zn.'t", {"movprfx_z_p_z"_h}},
      {"'Zd.'t, 'Pgl/m, 'Zd.'t, 'Zn.'t",
       {"addp_z_p_zz"_h,    "shadd_z_p_zz"_h,  "shsub_z_p_zz"_h,
        "shsubr_z_p_zz"_h,  "smaxp_z_p_zz"_h,  "sminp_z_p_zz"_h,
        "sqadd_z_p_zz"_h,   "sqrshl_z_p_zz"_h, "sqrshlr_z_p_zz"_h,
        "sqshl_z_p_zz"_h,   "sqshlr_z_p_zz"_h, "sqsub_z_p_zz"_h,
        "sqsubr_z_p_zz"_h,  "srhadd_z_p_zz"_h, "srshl_z_p_zz"_h,
        "srshlr_z_p_zz"_h,  "suqadd_z_p_zz"_h, "uhadd_z_p_zz"_h,
        "uhsub_z_p_zz"_h,   "uhsubr_z_p_zz"_h, "umaxp_z_p_zz"_h,
        "uminp_z_p_zz"_h,   "uqadd_z_p_zz"_h,  "uqrshl_z_p_zz"_h,
        "uqrshlr_z_p_zz"_h, "uqshl_z_p_zz"_h,  "uqshlr_z_p_zz"_h,
        "uqsub_z_p_zz"_h,   "uqsubr_z_p_zz"_h, "urhadd_z_p_zz"_h,
        "urshl_z_p_zz"_h,   "urshlr_z_p_zz"_h, "usqadd_z_p_zz"_h,
        "mul_z_p_zz"_h,     "smulh_z_p_zz"_h,  "umulh_z_p_zz"_h,
        "sabd_z_p_zz"_h,    "smax_z_p_zz"_h,   "smin_z_p_zz"_h,
        "uabd_z_p_zz"_h,    "umax_z_p_zz"_h,   "umin_z_p_zz"_h,
        "add_z_p_zz"_h,     "subr_z_p_zz"_h,   "sub_z_p_zz"_h,
        "and_z_p_zz"_h,     "bic_z_p_zz"_h,    "eor_z_p_zz"_h,
        "orr_z_p_zz"_h,     "asrr_z_p_zz"_h,   "asr_z_p_zz"_h,
        "lslr_z_p_zz"_h,    "lsl_z_p_zz"_h,    "lsrr_z_p_zz"_h,
        "lsr_z_p_zz"_h}},
      {"'Zd.'t, 'Pgl/m, 'Zm.'t, 'Zn.'t", {"mad_z_p_zzz"_h, "msb_z_p_zzz"_h}},
      {"'Zd.'t, 'Pgl/m, 'Zn.'t", {"sqabs_z_p_z"_h, "sqneg_z_p_z"_h}},
      {"'Zd.'t, 'Pgl/m, 'Zn.'t, 'Zm.'t", {"mla_z_p_zzz"_h, "mls_z_p_zzz"_h}},
      {"'Zd.'t, 'Pn",
       {"decp_z_p_z"_h,
        "incp_z_p_z"_h,
        "sqdecp_z_p_z"_h,
        "sqincp_z_p_z"_h,
        "uqdecp_z_p_z"_h,
        "uqincp_z_p_z"_h}},
      {"'Zd.'t, 'Vnv", {"insr_z_v"_h}},
      {"'Zd.'t, 'Zd.'t, #'s1205", {"mul_z_zi"_h, "smax_z_zi"_h, "smin_z_zi"_h}},
      {"'Zd.'t, 'Zd.'t, #'u1205", {"umax_z_zi"_h, "umin_z_zi"_h}},
      {"'Zd.'t, 'Zn.'t, 'Zm.'t",
       {"bdep_z_zz"_h,      "bext_z_zz"_h,      "bgrp_z_zz"_h,
        "eorbt_z_zz"_h,     "eortb_z_zz"_h,     "mul_z_zz"_h,
        "smulh_z_zz"_h,     "sqdmulh_z_zz"_h,   "sqrdmulh_z_zz"_h,
        "tbx_z_zz"_h,       "umulh_z_zz"_h,     "saba_z_zzz"_h,
        "sqrdmlah_z_zzz"_h, "sqrdmlsh_z_zzz"_h, "uaba_z_zzz"_h,
        "fmmla_z_zzz_s"_h,  "fmmla_z_zzz_d"_h,  "trn1_z_zz"_h,
        "trn2_z_zz"_h,      "uzp1_z_zz"_h,      "uzp2_z_zz"_h,
        "zip1_z_zz"_h,      "zip2_z_zz"_h,      "add_z_zz"_h,
        "sqadd_z_zz"_h,     "sqsub_z_zz"_h,     "sub_z_zz"_h,
        "uqadd_z_zz"_h,     "uqsub_z_zz"_h}},
      {"'Zd.'t, 'Zn.'t, 'Zm.'t, #'u1110*90",
       {"cmla_z_zzz"_h, "sqrdcmlah_z_zzz"_h}},
      {"'Zd.'t, {'Zn.'t, 'Zn2.'t}, 'Zm.'t", {"tbl_z_zz_2"_h}},
      {"'Zd.'t, {'Zn.'t}, 'Zm.'t", {"tbl_z_zz_1"_h}},
      {"'Zd.b, 'Zd.b", {"aesimc_z_z"_h, "aesmc_z_z"_h}},
      {"'Zd.b, 'Zd.b, 'Zn.b", {"aesd_z_zz"_h, "aese_z_zz"_h}},
      {"'Zd.b, 'Zd.b, 'Zn.b, #'u2016:1210", {"ext_z_zi_des"_h}},
      {"'Zd.b, {'Zn.b, 'Zn2.b}, #'u2016:1210", {"ext_z_zi_con"_h}},
      {"'Zd.d, 'Pgl/m, 'Zn.d", {"fcvtzs_z_p_z_d2x"_h, "fcvtzu_z_p_z_d2x"_h}},
      {"'Zd.d, 'Pgl/m, 'Zn.h",
       {"fcvtzs_z_p_z_fp162x"_h, "fcvtzu_z_p_z_fp162x"_h}},
      {"'Zd.d, 'Pgl/m, 'Zn.s",
       {"fcvtlt_z_p_z_s2d"_h, "fcvtzs_z_p_z_s2x"_h, "fcvtzu_z_p_z_s2x"_h}},
      {"'Zd.d, 'Zd.d, 'Zm.d, 'Zn.d",
       {"bcax_z_zzz"_h,
        "bsl1n_z_zzz"_h,
        "bsl2n_z_zzz"_h,
        "bsl_z_zzz"_h,
        "eor3_z_zzz"_h,
        "nbsl_z_zzz"_h}},
      {"'Zd.d, 'Zn.d, 'Zm.d", {"rax1_z_zz"_h}},
      {"'Zd.d, 'Zn.d, z'u1916.d['u2020]",
       {"fmla_z_zzzi_d"_h,
        "fmls_z_zzzi_d"_h,
        "fmul_z_zzi_d"_h,
        "mla_z_zzzi_d"_h,
        "mls_z_zzzi_d"_h,
        "mul_z_zzi_d"_h,
        "sqdmulh_z_zzi_d"_h,
        "sqrdmulh_z_zzi_d"_h,
        "sqrdmlah_z_zzzi_d"_h,
        "sqrdmlsh_z_zzzi_d"_h}},
      {"'Zd.d, 'Zn.h, z'u1916.h['u2020], #'u1110*90", {"cdot_z_zzzi_d"_h}},
      {"'Zd.d, 'Zn.s, z'u1916.s['u2020:1111]",
       {"smlalb_z_zzzi_d"_h,
        "smlalt_z_zzzi_d"_h,
        "smlslb_z_zzzi_d"_h,
        "smlslt_z_zzzi_d"_h,
        "smullb_z_zzi_d"_h,
        "smullt_z_zzi_d"_h,
        "sqdmullb_z_zzi_d"_h,
        "sqdmullt_z_zzi_d"_h,
        "sqdmlalb_z_zzzi_d"_h,
        "sqdmlalt_z_zzzi_d"_h,
        "sqdmlslb_z_zzzi_d"_h,
        "sqdmlslt_z_zzzi_d"_h,
        "umlalb_z_zzzi_d"_h,
        "umlalt_z_zzzi_d"_h,
        "umlslb_z_zzzi_d"_h,
        "umlslt_z_zzzi_d"_h,
        "umullb_z_zzi_d"_h,
        "umullt_z_zzi_d"_h}},
      {"'Zd.h, 'Pgl/m, 'Zn.h",
       {"fcvtzs_z_p_z_fp162h"_h, "fcvtzu_z_p_z_fp162h"_h}},
      {"'Zd.h, 'Pgl/m, 'Zn.s",
       {"fcvtnt_z_p_z_s2h"_h, "bfcvt_z_p_z_s2bf"_h, "bfcvtnt_z_p_z_s2bf"_h}},
      {"'Zd.h, 'Zn.h, z'u1816.h['u2019], #'u1110*90",
       {"cmla_z_zzzi_h"_h, "fcmla_z_zzzi_h"_h, "sqrdcmlah_z_zzzi_h"_h}},
      {"'Zd.h, 'Zn.h, z'u1816.h['u2222:2019]",
       {"fmla_z_zzzi_h"_h,
        "fmls_z_zzzi_h"_h,
        "fmul_z_zzi_h"_h,
        "mla_z_zzzi_h"_h,
        "mls_z_zzzi_h"_h,
        "mul_z_zzi_h"_h,
        "sqdmulh_z_zzi_h"_h,
        "sqrdmulh_z_zzi_h"_h,
        "sqrdmlah_z_zzzi_h"_h,
        "sqrdmlsh_z_zzzi_h"_h}},
      {"'Zd.q, 'Zn.d, 'Zm.d", {"pmullb_z_zz_q"_h, "pmullt_z_zz_q"_h}},
      {"'Zd.s, 'Pgl/m, 'Zn.d",
       {"fcvtnt_z_p_z_d2s"_h,
        "fcvtx_z_p_z_d2s"_h,
        "fcvtxnt_z_p_z_d2s"_h,
        "fcvtzs_z_p_z_d2w"_h,
        "fcvtzu_z_p_z_d2w"_h}},
      {"'Zd.s, 'Pgl/m, 'Zn.h",
       {"fcvtlt_z_p_z_h2s"_h,
        "fcvtzs_z_p_z_fp162w"_h,
        "fcvtzu_z_p_z_fp162w"_h}},
      {"'Zd.s, 'Pgl/m, 'Zn.s", {"fcvtzs_z_p_z_s2w"_h, "fcvtzu_z_p_z_s2w"_h}},
      {"'Zd.s, 'Zd.s, 'Zn.s", {"sm4e_z_zz"_h}},
      {"'Zd.s, 'Zn.b, 'Zm.b",
       {"smmla_z_zzz"_h, "ummla_z_zzz"_h, "usmmla_z_zzz"_h, "usdot_z_zzz_s"_h}},
      {"'Zd.s, 'Zn.b, z'u1816.b['u2019], #'u1110*90", {"cdot_z_zzzi_s"_h}},
      {"'Zd.s, 'Zn.h, 'Zm.h",
       {"fmlalb_z_zzz"_h,
        "fmlalt_z_zzz"_h,
        "fmlslb_z_zzz"_h,
        "fmlslt_z_zzz"_h,
        "bfdot_z_zzz"_h,
        "bfmlalb_z_zzz"_h,
        "bfmlalt_z_zzz"_h,
        "bfmmla_z_zzz"_h}},
      {"'Zd.s, 'Zn.h, z'u1816.h['u2019:1111]",
       {"fmlalb_z_zzzi_s"_h,   "fmlalt_z_zzzi_s"_h,   "fmlslb_z_zzzi_s"_h,
        "fmlslt_z_zzzi_s"_h,   "sqdmlalb_z_zzzi_s"_h, "sqdmlalt_z_zzzi_s"_h,
        "sqdmlslb_z_zzzi_s"_h, "sqdmlslt_z_zzzi_s"_h, "bfmlalb_z_zzzi"_h,
        "bfmlalt_z_zzzi"_h,    "smlalb_z_zzzi_s"_h,   "smlalt_z_zzzi_s"_h,
        "smlslb_z_zzzi_s"_h,   "smlslt_z_zzzi_s"_h,   "smullb_z_zzi_s"_h,
        "smullt_z_zzi_s"_h,    "sqdmullb_z_zzi_s"_h,  "sqdmullt_z_zzi_s"_h,
        "umlalb_z_zzzi_s"_h,   "umlalt_z_zzzi_s"_h,   "umlslb_z_zzzi_s"_h,
        "umlslt_z_zzzi_s"_h,   "umullb_z_zzi_s"_h,    "umullt_z_zzi_s"_h}},
      {"'Zd.s, 'Zn.h, z'u1816.h['u2019]", {"bfdot_z_zzzi"_h}},
      {"'Zd.s, 'Zn.s, 'Zm.s", {"sm4ekey_z_zz"_h}},
      {"'Zd.s, 'Zn.s, z'u1816.s['u2019]",
       {"fmla_z_zzzi_s"_h,
        "fmls_z_zzzi_s"_h,
        "fmul_z_zzi_s"_h,
        "mla_z_zzzi_s"_h,
        "mls_z_zzzi_s"_h,
        "mul_z_zzi_s"_h,
        "sqdmulh_z_zzi_s"_h,
        "sqrdmulh_z_zzi_s"_h,
        "sqrdmlah_z_zzzi_s"_h,
        "sqrdmlsh_z_zzzi_s"_h}},
      {"'Zd.s, 'Zn.s, z'u1916.s['u2020], #'u1110*90",
       {"cmla_z_zzzi_s"_h, "fcmla_z_zzzi_s"_h, "sqrdcmlah_z_zzzi_s"_h}},
      {"'prefOp, 'ILLiteral 'LValue", {"prfm_p_loadlit"_h}},
      {"'prefOp, ['Xns'ILS]", {"prfum_p_ldst_unscaled"_h}},
      {"'prefSVEOp, 'Pgl, ['Xns, 'Zm.s, '?22:suxtw #1]",
       {"prfh_i_p_bz_s_x32_scaled"_h}},
      {"'prefSVEOp, 'Pgl, ['Xns, 'Zm.s, '?22:suxtw #2]",
       {"prfw_i_p_bz_s_x32_scaled"_h}},
      {"'prefSVEOp, 'Pgl, ['Xns, 'Zm.s, '?22:suxtw #3]",
       {"prfd_i_p_bz_s_x32_scaled"_h}},
      {"'prefSVEOp, 'Pgl, ['Xns, 'Zm.s, '?22:suxtw]",
       {"prfb_i_p_bz_s_x32_scaled"_h}},
      {"'t'u0400, 'Pgl, 'Zn.'t", {"lasta_v_p_z"_h, "lastb_v_p_z"_h}},
      {"'t'u0400, 'Pgl, 't'u0400, 'Zn.'t",
       {"clasta_v_p_z"_h, "clastb_v_p_z"_h}},
      {"p'u1310, 'Pn.b", {"ptest_p_p"_h}},
      {"{'IDebug}",
       {"dcps1_dc_exception"_h,
        "dcps2_dc_exception"_h,
        "dcps3_dc_exception"_h}},
      {"{'Zt.'tlss}, 'Pgl/z, ['Xns, 'Xm, lsl #'u2423]",
       {"ld1d_z_p_br_u64"_h,
        "ld1h_z_p_br_u16"_h,
        "ld1h_z_p_br_u32"_h,
        "ld1h_z_p_br_u64"_h,
        "ld1w_z_p_br_u32"_h,
        "ld1w_z_p_br_u64"_h}},
      {"{'Zt.'tlss}, 'Pgl/z, ['Xns, 'Xm, lsl #1]",
       {"ld1sh_z_p_br_s32"_h, "ld1sh_z_p_br_s64"_h}},
      {"{'Zt.'tlss}, 'Pgl/z, ['Xns, 'Xm, lsl #2]", {"ld1sw_z_p_br_s64"_h}},
      {"{'Zt.'tlss}, 'Pgl/z, ['Xns, 'Xm]",
       {"ld1b_z_p_br_u16"_h,
        "ld1b_z_p_br_u32"_h,
        "ld1b_z_p_br_u64"_h,
        "ld1b_z_p_br_u8"_h,
        "ld1sb_z_p_br_s16"_h,
        "ld1sb_z_p_br_s32"_h,
        "ld1sb_z_p_br_s64"_h}},
      {"{'Zt.'tls}, 'Pgl, ['Xns, 'Xm'NSveS]",
       {"st1b_z_p_br"_h, "st1d_z_p_br"_h, "st1h_z_p_br"_h, "st1w_z_p_br"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz, 'Zt4.'tmsz}, 'Pgl, ['Xns'ISveSvl]",
       {"st4b_z_p_bi_contiguous"_h,
        "st4d_z_p_bi_contiguous"_h,
        "st4h_z_p_bi_contiguous"_h,
        "st4w_z_p_bi_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz, 'Zt4.'tmsz}, 'Pgl, ['Xns, "
       "'Xm'NSveS]",
       {"st4b_z_p_br_contiguous"_h,
        "st4d_z_p_br_contiguous"_h,
        "st4h_z_p_br_contiguous"_h,
        "st4w_z_p_br_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz, 'Zt4.'tmsz}, 'Pgl/z, "
       "['Xns'ISveSvl]",
       {"ld4b_z_p_bi_contiguous"_h,
        "ld4d_z_p_bi_contiguous"_h,
        "ld4h_z_p_bi_contiguous"_h,
        "ld4w_z_p_bi_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz, 'Zt4.'tmsz}, 'Pgl/z, ['Xns, "
       "'Xm'NSveS]",
       {"ld4b_z_p_br_contiguous"_h,
        "ld4d_z_p_br_contiguous"_h,
        "ld4h_z_p_br_contiguous"_h,
        "ld4w_z_p_br_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz}, 'Pgl, ['Xns'ISveSvl]",
       {"st3b_z_p_bi_contiguous"_h,
        "st3d_z_p_bi_contiguous"_h,
        "st3h_z_p_bi_contiguous"_h,
        "st3w_z_p_bi_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz}, 'Pgl, ['Xns, 'Xm'NSveS]",
       {"st3b_z_p_br_contiguous"_h,
        "st3d_z_p_br_contiguous"_h,
        "st3h_z_p_br_contiguous"_h,
        "st3w_z_p_br_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz}, 'Pgl/z, ['Xns'ISveSvl]",
       {"ld3b_z_p_bi_contiguous"_h,
        "ld3d_z_p_bi_contiguous"_h,
        "ld3h_z_p_bi_contiguous"_h,
        "ld3w_z_p_bi_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz, 'Zt3.'tmsz}, 'Pgl/z, ['Xns, 'Xm'NSveS]",
       {"ld3b_z_p_br_contiguous"_h,
        "ld3d_z_p_br_contiguous"_h,
        "ld3h_z_p_br_contiguous"_h,
        "ld3w_z_p_br_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz}, 'Pgl, ['Xns'ISveSvl]",
       {"st2b_z_p_bi_contiguous"_h,
        "st2d_z_p_bi_contiguous"_h,
        "st2h_z_p_bi_contiguous"_h,
        "st2w_z_p_bi_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz}, 'Pgl, ['Xns, 'Xm'NSveS]",
       {"st2b_z_p_br_contiguous"_h,
        "st2d_z_p_br_contiguous"_h,
        "st2h_z_p_br_contiguous"_h,
        "st2w_z_p_br_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz}, 'Pgl/z, ['Xns'ISveSvl]",
       {"ld2b_z_p_bi_contiguous"_h,
        "ld2d_z_p_bi_contiguous"_h,
        "ld2h_z_p_bi_contiguous"_h,
        "ld2w_z_p_bi_contiguous"_h}},
      {"{'Zt.'tmsz, 'Zt2.'tmsz}, 'Pgl/z, ['Xns, 'Xm'NSveS]",
       {"ld2b_z_p_br_contiguous"_h,
        "ld2d_z_p_br_contiguous"_h,
        "ld2h_z_p_br_contiguous"_h,
        "ld2w_z_p_br_contiguous"_h}},
      {"{'Zt.'tmsz}, 'Pgl/z, ['Xns, 'Rm, lsl #'u2423]",
       {"ld1rqd_z_p_br_contiguous"_h,
        "ld1rqh_z_p_br_contiguous"_h,
        "ld1rqw_z_p_br_contiguous"_h,
        "ld1rod_z_p_br_contiguous"_h,
        "ld1roh_z_p_br_contiguous"_h,
        "ld1row_z_p_br_contiguous"_h}},
      {"{'Zt.'tmsz}, 'Pgl/z, ['Xns, 'Rm]",
       {"ld1rqb_z_p_br_contiguous"_h, "ld1rob_z_p_br_contiguous"_h}},
      {"{'Zt.b}, 'Pgl, ['Xns, 'Rm]", {"stnt1b_z_p_br_contiguous"_h}},
      {"{'Zt.b}, 'Pgl/z, ['Xns, 'Rm]", {"ldnt1b_z_p_br_contiguous"_h}},
      {"{'Zt.d}, 'Pgl, ['Xns, 'Rm, lsl #3]", {"stnt1d_z_p_br_contiguous"_h}},
      {"{'Zt.d}, 'Pgl, ['Xns, 'Zm.d, '?14:suxtw #'u2423]",
       {"st1d_z_p_bz_d_x32_scaled"_h,
        "st1h_z_p_bz_d_x32_scaled"_h,
        "st1w_z_p_bz_d_x32_scaled"_h}},
      {"{'Zt.d}, 'Pgl, ['Xns, 'Zm.d, '?14:suxtw]",
       {"st1b_z_p_bz_d_x32_unscaled"_h,
        "st1d_z_p_bz_d_x32_unscaled"_h,
        "st1h_z_p_bz_d_x32_unscaled"_h,
        "st1w_z_p_bz_d_x32_unscaled"_h}},
      {"{'Zt.d}, 'Pgl, ['Xns, 'Zm.d, lsl #'u2423]",
       {"st1d_z_p_bz_d_64_scaled"_h,
        "st1h_z_p_bz_d_64_scaled"_h,
        "st1w_z_p_bz_d_64_scaled"_h}},
      {"{'Zt.d}, 'Pgl, ['Xns, 'Zm.d]",
       {"st1b_z_p_bz_d_64_unscaled"_h,
        "st1d_z_p_bz_d_64_unscaled"_h,
        "st1h_z_p_bz_d_64_unscaled"_h,
        "st1w_z_p_bz_d_64_unscaled"_h}},
      {"{'Zt.d}, 'Pgl/z, ['Xns, 'Rm, lsl #3]", {"ldnt1d_z_p_br_contiguous"_h}},
      {"{'Zt.d}, 'Pgl/z, ['Xns, 'Zm.d, '?22:suxtw #'u2423]",
       {"ld1d_z_p_bz_d_x32_scaled"_h,
        "ld1h_z_p_bz_d_x32_scaled"_h,
        "ld1sh_z_p_bz_d_x32_scaled"_h,
        "ld1sw_z_p_bz_d_x32_scaled"_h,
        "ld1w_z_p_bz_d_x32_scaled"_h,
        "ldff1d_z_p_bz_d_x32_scaled"_h,
        "ldff1h_z_p_bz_d_x32_scaled"_h,
        "ldff1sh_z_p_bz_d_x32_scaled"_h,
        "ldff1sw_z_p_bz_d_x32_scaled"_h,
        "ldff1w_z_p_bz_d_x32_scaled"_h}},
      {"{'Zt.d}, 'Pgl/z, ['Xns, 'Zm.d, '?22:suxtw]",
       {"ld1b_z_p_bz_d_x32_unscaled"_h,
        "ld1d_z_p_bz_d_x32_unscaled"_h,
        "ld1h_z_p_bz_d_x32_unscaled"_h,
        "ld1sb_z_p_bz_d_x32_unscaled"_h,
        "ld1sh_z_p_bz_d_x32_unscaled"_h,
        "ld1sw_z_p_bz_d_x32_unscaled"_h,
        "ld1w_z_p_bz_d_x32_unscaled"_h,
        "ldff1b_z_p_bz_d_x32_unscaled"_h,
        "ldff1d_z_p_bz_d_x32_unscaled"_h,
        "ldff1h_z_p_bz_d_x32_unscaled"_h,
        "ldff1sb_z_p_bz_d_x32_unscaled"_h,
        "ldff1sh_z_p_bz_d_x32_unscaled"_h,
        "ldff1sw_z_p_bz_d_x32_unscaled"_h,
        "ldff1w_z_p_bz_d_x32_unscaled"_h}},
      {"{'Zt.d}, 'Pgl/z, ['Xns, 'Zm.d, lsl #'u2423]",
       {"ld1d_z_p_bz_d_64_scaled"_h,
        "ld1h_z_p_bz_d_64_scaled"_h,
        "ld1sh_z_p_bz_d_64_scaled"_h,
        "ld1sw_z_p_bz_d_64_scaled"_h,
        "ld1w_z_p_bz_d_64_scaled"_h,
        "ldff1d_z_p_bz_d_64_scaled"_h,
        "ldff1h_z_p_bz_d_64_scaled"_h,
        "ldff1sh_z_p_bz_d_64_scaled"_h,
        "ldff1sw_z_p_bz_d_64_scaled"_h,
        "ldff1w_z_p_bz_d_64_scaled"_h}},
      {"{'Zt.d}, 'Pgl/z, ['Xns, 'Zm.d]",
       {"ld1b_z_p_bz_d_64_unscaled"_h,
        "ld1d_z_p_bz_d_64_unscaled"_h,
        "ld1h_z_p_bz_d_64_unscaled"_h,
        "ld1sb_z_p_bz_d_64_unscaled"_h,
        "ld1sh_z_p_bz_d_64_unscaled"_h,
        "ld1sw_z_p_bz_d_64_unscaled"_h,
        "ld1w_z_p_bz_d_64_unscaled"_h,
        "ldff1b_z_p_bz_d_64_unscaled"_h,
        "ldff1d_z_p_bz_d_64_unscaled"_h,
        "ldff1h_z_p_bz_d_64_unscaled"_h,
        "ldff1sb_z_p_bz_d_64_unscaled"_h,
        "ldff1sh_z_p_bz_d_64_unscaled"_h,
        "ldff1sw_z_p_bz_d_64_unscaled"_h,
        "ldff1w_z_p_bz_d_64_unscaled"_h}},
      {"{'Zt.h}, 'Pgl, ['Xns, 'Rm, lsl #1]", {"stnt1h_z_p_br_contiguous"_h}},
      {"{'Zt.h}, 'Pgl/z, ['Xns, 'Rm, lsl #1]", {"ldnt1h_z_p_br_contiguous"_h}},
      {"{'Zt.s}, 'Pgl, ['Xns, 'Rm, lsl #2]", {"stnt1w_z_p_br_contiguous"_h}},
      {"{'Zt.s}, 'Pgl, ['Xns, 'Zm.s, '?14:suxtw #'u2423]",
       {"st1h_z_p_bz_s_x32_scaled"_h, "st1w_z_p_bz_s_x32_scaled"_h}},
      {"{'Zt.s}, 'Pgl, ['Xns, 'Zm.s, '?14:suxtw]",
       {"st1b_z_p_bz_s_x32_unscaled"_h,
        "st1h_z_p_bz_s_x32_unscaled"_h,
        "st1w_z_p_bz_s_x32_unscaled"_h}},
      {"{'Zt.s}, 'Pgl/z, ['Xns, 'Rm, lsl #2]", {"ldnt1w_z_p_br_contiguous"_h}},
      {"{'Zt.s}, 'Pgl/z, ['Xns, 'Zm.s, '?22:suxtw #1]",
       {"ld1h_z_p_bz_s_x32_scaled"_h,
        "ld1sh_z_p_bz_s_x32_scaled"_h,
        "ldff1h_z_p_bz_s_x32_scaled"_h,
        "ldff1sh_z_p_bz_s_x32_scaled"_h}},
      {"{'Zt.s}, 'Pgl/z, ['Xns, 'Zm.s, '?22:suxtw #2]",
       {"ld1w_z_p_bz_s_x32_scaled"_h, "ldff1w_z_p_bz_s_x32_scaled"_h}},
      {"'Hd, 'Hn, 'Vf.h['IVByElemIndex]",
       {"fmla_asisdelem_rh_h"_h,
        "fmls_asisdelem_rh_h"_h,
        "fmul_asisdelem_rh_h"_h,
        "fmulx_asisdelem_rh_h"_h}},
      {"{'Zt.s}, 'Pgl/z, ['Xns, 'Zm.s, '?22:suxtw]",
       {"ld1b_z_p_bz_s_x32_unscaled"_h,
        "ld1h_z_p_bz_s_x32_unscaled"_h,
        "ld1sb_z_p_bz_s_x32_unscaled"_h,
        "ld1sh_z_p_bz_s_x32_unscaled"_h,
        "ld1w_z_p_bz_s_x32_unscaled"_h,
        "ldff1b_z_p_bz_s_x32_unscaled"_h,
        "ldff1h_z_p_bz_s_x32_unscaled"_h,
        "ldff1sb_z_p_bz_s_x32_unscaled"_h,
        "ldff1sh_z_p_bz_s_x32_unscaled"_h,
        "ldff1w_z_p_bz_s_x32_unscaled"_h}}};

  for (auto &itm : forms) {
    const std::unordered_set<uint32_t> &s = forms.at(itm.first);
    for (const uint32_t &its : s) {
      fts->insert(std::make_pair(its, itm.first.c_str()));
    }
  }
}

const Disassembler::FormToVisitorFnMap *Disassembler::GetFormToVisitorFnMap() {
  static const FormToVisitorFnMap form_to_visitor = {
      DEFAULT_FORM_TO_VISITOR_MAP(Disassembler),
      {"fcvt_dh_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fcvt_ds_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fcvt_hd_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fcvt_hs_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fcvt_sd_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fcvt_sh_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"bfcvt_bs_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fmov_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fmov_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"fmov_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint32x_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint32x_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint32z_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint32z_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint64x_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint64x_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint64z_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frint64z_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frinta_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frinta_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frinta_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frinti_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frinti_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frinti_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintm_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintm_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintm_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintn_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintn_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintn_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintp_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintp_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintp_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintx_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintx_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintx_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintz_d_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintz_h_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"frintz_s_floatdp1"_h, &Disassembler::VisitFPDataProcessing1Source},
      {"abs_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"cls_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"clz_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"cnt_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"neg_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"rev16_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"rev32_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"rev64_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"sqabs_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"sqneg_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"suqadd_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"urecpe_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"ursqrte_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"usqadd_asimdmisc_r"_h, &Disassembler::VisitNEON2RegMisc},
      {"not_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegLogical},
      {"rbit_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegLogical},
      {"xtn_asimdmisc_n"_h, &Disassembler::DisassembleNEON2RegExtract},
      {"sqxtn_asimdmisc_n"_h, &Disassembler::DisassembleNEON2RegExtract},
      {"uqxtn_asimdmisc_n"_h, &Disassembler::DisassembleNEON2RegExtract},
      {"sqxtun_asimdmisc_n"_h, &Disassembler::DisassembleNEON2RegExtract},
      {"shll_asimdmisc_s"_h, &Disassembler::DisassembleNEON2RegExtract},
      {"sadalp_asimdmisc_p"_h, &Disassembler::DisassembleNEON2RegAddlp},
      {"saddlp_asimdmisc_p"_h, &Disassembler::DisassembleNEON2RegAddlp},
      {"uadalp_asimdmisc_p"_h, &Disassembler::DisassembleNEON2RegAddlp},
      {"uaddlp_asimdmisc_p"_h, &Disassembler::DisassembleNEON2RegAddlp},
      {"cmeq_asimdmisc_z"_h, &Disassembler::DisassembleNEON2RegCompare},
      {"cmge_asimdmisc_z"_h, &Disassembler::DisassembleNEON2RegCompare},
      {"cmgt_asimdmisc_z"_h, &Disassembler::DisassembleNEON2RegCompare},
      {"cmle_asimdmisc_z"_h, &Disassembler::DisassembleNEON2RegCompare},
      {"cmlt_asimdmisc_z"_h, &Disassembler::DisassembleNEON2RegCompare},
      {"fcmeq_asimdmisc_fz"_h, &Disassembler::DisassembleNEON2RegFPCompare},
      {"fcmge_asimdmisc_fz"_h, &Disassembler::DisassembleNEON2RegFPCompare},
      {"fcmgt_asimdmisc_fz"_h, &Disassembler::DisassembleNEON2RegFPCompare},
      {"fcmle_asimdmisc_fz"_h, &Disassembler::DisassembleNEON2RegFPCompare},
      {"fcmlt_asimdmisc_fz"_h, &Disassembler::DisassembleNEON2RegFPCompare},
      {"fcvtl_asimdmisc_l"_h, &Disassembler::DisassembleNEON2RegFPConvert},
      {"fcvtn_asimdmisc_n"_h, &Disassembler::DisassembleNEON2RegFPConvert},
      {"fcvtxn_asimdmisc_n"_h, &Disassembler::DisassembleNEON2RegFPConvert},
      {"bfcvtn_asimdmisc_4s"_h, &Disassembler::DisassembleNEON2RegFPConvert},
      {"fabs_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtas_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtau_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtms_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtmu_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtns_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtnu_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtps_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtpu_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtzs_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fcvtzu_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fneg_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frecpe_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frint32x_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frint32z_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frint64x_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frint64z_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frinta_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frinti_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frintm_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frintn_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frintp_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frintx_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frintz_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"frsqrte_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"fsqrt_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"scvtf_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"ucvtf_asimdmisc_r"_h, &Disassembler::DisassembleNEON2RegFP},
      {"smlal_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"smlsl_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"smull_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"umlal_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"umlsl_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"umull_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"sqdmull_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"sqdmlal_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"sqdmlsl_asimdelem_l"_h, &Disassembler::DisassembleNEONMulByElementLong},
      {"sdot_asimdelem_d"_h, &Disassembler::DisassembleNEONDotProdByElement},
      {"udot_asimdelem_d"_h, &Disassembler::DisassembleNEONDotProdByElement},
      {"usdot_asimdelem_d"_h, &Disassembler::DisassembleNEONDotProdByElement},
      {"sudot_asimdelem_d"_h, &Disassembler::DisassembleNEONDotProdByElement},
      {"fcmla_asimdelem_c_h"_h,
       &Disassembler::DisassembleNEONComplexMulByElement},
      {"fcmla_asimdelem_c_s"_h,
       &Disassembler::DisassembleNEONComplexMulByElement},
      {"fmla_asimdelem_r_sd"_h, &Disassembler::DisassembleNEONFPMulByElement},
      {"fmls_asimdelem_r_sd"_h, &Disassembler::DisassembleNEONFPMulByElement},
      {"fmulx_asimdelem_r_sd"_h, &Disassembler::DisassembleNEONFPMulByElement},
      {"fmul_asimdelem_r_sd"_h, &Disassembler::DisassembleNEONFPMulByElement},
      {"mla_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"mls_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"mul_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"saba_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"sabd_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"shadd_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"shsub_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"smaxp_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"smax_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"sminp_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"smin_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"srhadd_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"uaba_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"uabd_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"uhadd_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"uhsub_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"umaxp_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"umax_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"uminp_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"umin_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"urhadd_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameNoD},
      {"and_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"bic_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"bif_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"bit_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"bsl_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"eor_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"orr_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"orn_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"pmul_asimdsame_only"_h, &Disassembler::DisassembleNEON3SameLogical},
      {"sri_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"srshr_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"srsra_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"sshr_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"ssra_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"urshr_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"ursra_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"ushr_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"usra_asimdshf_r"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"scvtf_asimdshf_c"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"ucvtf_asimdshf_c"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"fcvtzs_asimdshf_c"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"fcvtzu_asimdshf_c"_h, &Disassembler::DisassembleNEONShiftRightImm},
      {"ushll_asimdshf_l"_h, &Disassembler::DisassembleNEONShiftLeftLongImm},
      {"sshll_asimdshf_l"_h, &Disassembler::DisassembleNEONShiftLeftLongImm},
      {"shrn_asimdshf_n"_h, &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"rshrn_asimdshf_n"_h, &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"sqshrn_asimdshf_n"_h,
       &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"sqrshrn_asimdshf_n"_h,
       &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"sqshrun_asimdshf_n"_h,
       &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"sqrshrun_asimdshf_n"_h,
       &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"uqshrn_asimdshf_n"_h,
       &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"uqrshrn_asimdshf_n"_h,
       &Disassembler::DisassembleNEONShiftRightNarrowImm},
      {"sqdmlal_asisdelem_l"_h,
       &Disassembler::DisassembleNEONScalarSatMulLongIndex},
      {"sqdmlsl_asisdelem_l"_h,
       &Disassembler::DisassembleNEONScalarSatMulLongIndex},
      {"sqdmull_asisdelem_l"_h,
       &Disassembler::DisassembleNEONScalarSatMulLongIndex},
      {"sqrdmlah_asisdsame2_only"_h, &Disassembler::VisitNEONScalar3Same},
      {"sqrdmlsh_asisdsame2_only"_h, &Disassembler::VisitNEONScalar3Same},
      {"cmeq_asisdsame_only"_h, &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"cmge_asisdsame_only"_h, &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"cmgt_asisdsame_only"_h, &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"cmhi_asisdsame_only"_h, &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"cmhs_asisdsame_only"_h, &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"cmtst_asisdsame_only"_h,
       &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"add_asisdsame_only"_h, &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"sub_asisdsame_only"_h, &Disassembler::DisassembleNEONScalar3SameOnlyD},
      {"fmaxnmv_asimdall_only_sd"_h,
       &Disassembler::DisassembleNEONFPAcrossLanes},
      {"fminnmv_asimdall_only_sd"_h,
       &Disassembler::DisassembleNEONFPAcrossLanes},
      {"fmaxv_asimdall_only_sd"_h, &Disassembler::DisassembleNEONFPAcrossLanes},
      {"fminv_asimdall_only_sd"_h, &Disassembler::DisassembleNEONFPAcrossLanes},
      {"shl_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"sli_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"sri_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"srshr_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"srsra_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"sshr_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"ssra_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"urshr_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"ursra_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"ushr_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"usra_asisdshf_r"_h, &Disassembler::DisassembleNEONScalarShiftImmOnlyD},
      {"sqrshrn_asisdshf_n"_h,
       &Disassembler::DisassembleNEONScalarShiftRightNarrowImm},
      {"sqrshrun_asisdshf_n"_h,
       &Disassembler::DisassembleNEONScalarShiftRightNarrowImm},
      {"sqshrn_asisdshf_n"_h,
       &Disassembler::DisassembleNEONScalarShiftRightNarrowImm},
      {"sqshrun_asisdshf_n"_h,
       &Disassembler::DisassembleNEONScalarShiftRightNarrowImm},
      {"uqrshrn_asisdshf_n"_h,
       &Disassembler::DisassembleNEONScalarShiftRightNarrowImm},
      {"uqshrn_asisdshf_n"_h,
       &Disassembler::DisassembleNEONScalarShiftRightNarrowImm},
      {"cmeq_asisdmisc_z"_h, &Disassembler::DisassembleNEONScalar2RegMiscOnlyD},
      {"cmge_asisdmisc_z"_h, &Disassembler::DisassembleNEONScalar2RegMiscOnlyD},
      {"cmgt_asisdmisc_z"_h, &Disassembler::DisassembleNEONScalar2RegMiscOnlyD},
      {"cmle_asisdmisc_z"_h, &Disassembler::DisassembleNEONScalar2RegMiscOnlyD},
      {"cmlt_asisdmisc_z"_h, &Disassembler::DisassembleNEONScalar2RegMiscOnlyD},
      {"abs_asisdmisc_r"_h, &Disassembler::DisassembleNEONScalar2RegMiscOnlyD},
      {"neg_asisdmisc_r"_h, &Disassembler::DisassembleNEONScalar2RegMiscOnlyD},
      {"fcvtxn_asisdmisc_n"_h, &Disassembler::DisassembleNEONFPScalar2RegMisc},
      {"pmull_asimddiff_l"_h, &Disassembler::DisassembleNEONPolynomialMul},
      {"addhnb_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"addhnt_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"cadd_z_zz"_h, &Disassembler::DisassembleSVEComplexIntAddition},
      {"cdot_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb_const},
      {"faddp_z_p_zz"_h, &Disassembler::DisassembleSVEFPPair},
      {"flogb_z_p_z"_h, &Disassembler::DisassembleSVEFlogb},
      {"fmaxnmp_z_p_zz"_h, &Disassembler::DisassembleSVEFPPair},
      {"fmaxp_z_p_zz"_h, &Disassembler::DisassembleSVEFPPair},
      {"fminnmp_z_p_zz"_h, &Disassembler::DisassembleSVEFPPair},
      {"fminp_z_p_zz"_h, &Disassembler::DisassembleSVEFPPair},
      {"histcnt_z_p_zz"_h, &Disassembler::Disassemble_ZdT_PgZ_ZnT_ZmT},
      {"histseg_z_zz"_h, &Disassembler::Disassemble_ZdB_ZnB_ZmB},
      {"ldnt1b_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm},
      {"ldnt1b_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_PgZ_ZnS_Xm},
      {"ldnt1d_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm},
      {"ldnt1h_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm},
      {"ldnt1h_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_PgZ_ZnS_Xm},
      {"ldnt1sb_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm},
      {"ldnt1sb_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_PgZ_ZnS_Xm},
      {"ldnt1sh_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm},
      {"ldnt1sh_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_PgZ_ZnS_Xm},
      {"ldnt1sw_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm},
      {"ldnt1w_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm},
      {"ldnt1w_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_PgZ_ZnS_Xm},
      {"match_p_p_zz"_h, &Disassembler::Disassemble_PdT_PgZ_ZnT_ZmT},
      {"nmatch_p_p_zz"_h, &Disassembler::Disassemble_PdT_PgZ_ZnT_ZmT},
      {"pmul_z_zz"_h, &Disassembler::Disassemble_ZdB_ZnB_ZmB},
      {"pmullb_z_zz"_h, &Disassembler::DisassembleSVEPmull},
      {"pmullt_z_zz"_h, &Disassembler::DisassembleSVEPmull},
      {"raddhnb_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"raddhnt_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"rshrnb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"rshrnt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"rsubhnb_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"rsubhnt_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"sabalb_z_zzz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"sabalt_z_zzz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"sabdlb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"sabdlt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"sadalp_z_p_z"_h, &Disassembler::Disassemble_ZdaT_PgM_ZnTb},
      {"saddlb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"saddlbt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"saddlt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"saddwb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"saddwt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"shrnb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"shrnt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sli_z_zzi"_h, &Disassembler::VisitSVEBitwiseShiftUnpredicated},
      {"smlalb_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"smlalt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"smlslb_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"smlslt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"smullb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"smullt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"sqcadd_z_zz"_h, &Disassembler::DisassembleSVEComplexIntAddition},
      {"sqdmlalb_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"sqdmlalbt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"sqdmlalt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"sqdmlslb_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"sqdmlslbt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"sqdmlslt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"sqdmullb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"sqdmullt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"sqrshrnb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqrshrnt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqrshrunb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqrshrunt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqshl_z_p_zi"_h, &Disassembler::VisitSVEBitwiseShiftByImm_Predicated},
      {"sqshlu_z_p_zi"_h, &Disassembler::VisitSVEBitwiseShiftByImm_Predicated},
      {"sqshrnb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqshrnt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqshrunb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqshrunt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"sqxtnb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb},
      {"sqxtnt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb},
      {"sqxtunb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb},
      {"sqxtunt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb},
      {"sri_z_zzi"_h, &Disassembler::VisitSVEBitwiseShiftUnpredicated},
      {"srshr_z_p_zi"_h, &Disassembler::VisitSVEBitwiseShiftByImm_Predicated},
      {"srsra_z_zi"_h, &Disassembler::VisitSVEBitwiseShiftUnpredicated},
      {"sshllb_z_zi"_h, &Disassembler::DisassembleSVEShiftLeftImm},
      {"sshllt_z_zi"_h, &Disassembler::DisassembleSVEShiftLeftImm},
      {"ssra_z_zi"_h, &Disassembler::VisitSVEBitwiseShiftUnpredicated},
      {"ssublb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"ssublbt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"ssublt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"ssubltb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"ssubwb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"ssubwt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"stnt1b_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_Pg_ZnD_Xm},
      {"stnt1b_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_Pg_ZnS_Xm},
      {"stnt1d_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_Pg_ZnD_Xm},
      {"stnt1h_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_Pg_ZnD_Xm},
      {"stnt1h_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_Pg_ZnS_Xm},
      {"stnt1w_z_p_ar_d_64_unscaled"_h,
       &Disassembler::Disassemble_ZtD_Pg_ZnD_Xm},
      {"stnt1w_z_p_ar_s_x32_unscaled"_h,
       &Disassembler::Disassemble_ZtS_Pg_ZnS_Xm},
      {"subhnb_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"subhnt_z_zz"_h, &Disassembler::DisassembleSVEAddSubHigh},
      {"uabalb_z_zzz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"uabalt_z_zzz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"uabdlb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"uabdlt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"uadalp_z_p_z"_h, &Disassembler::Disassemble_ZdaT_PgM_ZnTb},
      {"uaddlb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"uaddlt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"uaddwb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"uaddwt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"umlalb_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"umlalt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"umlslb_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"umlslt_z_zzz"_h, &Disassembler::Disassemble_ZdaT_ZnTb_ZmTb},
      {"umullb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"umullt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"uqrshrnb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"uqrshrnt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"uqshl_z_p_zi"_h, &Disassembler::VisitSVEBitwiseShiftByImm_Predicated},
      {"uqshrnb_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"uqshrnt_z_zi"_h, &Disassembler::DisassembleSVEShiftRightImm},
      {"uqxtnb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb},
      {"uqxtnt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb},
      {"urecpe_z_p_z"_h, &Disassembler::Disassemble_ZdS_PgM_ZnS},
      {"urshr_z_p_zi"_h, &Disassembler::VisitSVEBitwiseShiftByImm_Predicated},
      {"ursqrte_z_p_z"_h, &Disassembler::Disassemble_ZdS_PgM_ZnS},
      {"ursra_z_zi"_h, &Disassembler::VisitSVEBitwiseShiftUnpredicated},
      {"ushllb_z_zi"_h, &Disassembler::DisassembleSVEShiftLeftImm},
      {"ushllt_z_zi"_h, &Disassembler::DisassembleSVEShiftLeftImm},
      {"usra_z_zi"_h, &Disassembler::VisitSVEBitwiseShiftUnpredicated},
      {"usublb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"usublt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnTb_ZmTb},
      {"usubwb_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"usubwt_z_zz"_h, &Disassembler::Disassemble_ZdT_ZnT_ZmTb},
      {"whilege_p_p_rr"_h,
       &Disassembler::VisitSVEIntCompareScalarCountAndLimit},
      {"whilegt_p_p_rr"_h,
       &Disassembler::VisitSVEIntCompareScalarCountAndLimit},
      {"whilehi_p_p_rr"_h,
       &Disassembler::VisitSVEIntCompareScalarCountAndLimit},
      {"whilehs_p_p_rr"_h,
       &Disassembler::VisitSVEIntCompareScalarCountAndLimit},
      {"whilerw_p_rr"_h, &Disassembler::VisitSVEIntCompareScalarCountAndLimit},
      {"whilewr_p_rr"_h, &Disassembler::VisitSVEIntCompareScalarCountAndLimit},
      {"xar_z_zzi"_h, &Disassembler::Disassemble_ZdnT_ZdnT_ZmT_const},
      {"ld1row_z_p_bi_u32"_h,
       &Disassembler::VisitSVELoadAndBroadcastQOWord_ScalarPlusImm},
      {"ld1rod_z_p_bi_u64"_h,
       &Disassembler::VisitSVELoadAndBroadcastQOWord_ScalarPlusImm},
      {"ld1rob_z_p_bi_u8"_h,
       &Disassembler::VisitSVELoadAndBroadcastQOWord_ScalarPlusImm},
      {"ld1roh_z_p_bi_u16"_h,
       &Disassembler::VisitSVELoadAndBroadcastQOWord_ScalarPlusImm},
      {"usdot_z_zzzi_s"_h, &Disassembler::VisitSVEMulIndex},
      {"sudot_z_zzzi_s"_h, &Disassembler::VisitSVEMulIndex},
      {"usdot_asimdsame2_d"_h, &Disassembler::VisitNEON3SameExtra},
      {"addg_64_addsub_immtags"_h,
       &Disassembler::Disassemble_XdSP_XnSP_uimm6_uimm4},
      {"irg_64i_dp_2src"_h, &Disassembler::Disassemble_XdSP_XnSP_Xm},
      {"ldg_64loffset_ldsttags"_h, &Disassembler::DisassembleMTELoadTag},
      {"st2g_64soffset_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"st2g_64spost_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"st2g_64spre_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stgp_64_ldstpair_off"_h, &Disassembler::DisassembleMTEStoreTagPair},
      {"stgp_64_ldstpair_post"_h, &Disassembler::DisassembleMTEStoreTagPair},
      {"stgp_64_ldstpair_pre"_h, &Disassembler::DisassembleMTEStoreTagPair},
      {"stg_64soffset_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stg_64spost_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stg_64spre_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stz2g_64soffset_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stz2g_64spost_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stz2g_64spre_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stzg_64soffset_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stzg_64spost_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"stzg_64spre_ldsttags"_h, &Disassembler::DisassembleMTEStoreTag},
      {"subg_64_addsub_immtags"_h,
       &Disassembler::Disassemble_XdSP_XnSP_uimm6_uimm4},
      {"subps_64s_dp_2src"_h, &Disassembler::Disassemble_Xd_XnSP_XmSP},
      {"subp_64s_dp_2src"_h, &Disassembler::Disassemble_Xd_XnSP_XmSP},
      {"cpyen_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyern_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyewn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpye_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfen_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfern_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfewn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfe_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfmn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfmrn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfmwn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfm_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfpn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfprn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfpwn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyfp_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpymn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpymrn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpymwn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpym_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpypn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyprn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpypwn_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"cpyp_cpy_memcms"_h, &Disassembler::DisassembleCpy},
      {"seten_set_memcms"_h, &Disassembler::DisassembleSet},
      {"sete_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setgen_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setge_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setgmn_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setgm_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setgpn_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setgp_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setmn_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setm_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setpn_set_memcms"_h, &Disassembler::DisassembleSet},
      {"setp_set_memcms"_h, &Disassembler::DisassembleSet},
  };
  return &form_to_visitor;
}  // NOLINT(readability/fn_size)

Disassembler::Disassembler() {
  buffer_size_ = 256;
  buffer_ = reinterpret_cast<char *>(malloc(buffer_size_));
  buffer_pos_ = 0;
  own_buffer_ = true;
  code_address_offset_ = 0;

  PopulateFormToStringMap(&form_to_string_);
}

Disassembler::Disassembler(char *text_buffer, int buffer_size) {
  buffer_size_ = buffer_size;
  buffer_ = text_buffer;
  buffer_pos_ = 0;
  own_buffer_ = false;
  code_address_offset_ = 0;

  PopulateFormToStringMap(&form_to_string_);
}

Disassembler::~Disassembler() {
  if (own_buffer_) {
    free(buffer_);
  }
}

char *Disassembler::GetOutput() { return buffer_; }

void Disassembler::VisitAddSubImmediate(const Instruction *instr) {
  bool rd_is_zr = RdIsZROrSP(instr);
  bool stack_op =
      (rd_is_zr || RnIsZROrSP(instr)) && (instr->GetImmAddSub() == 0) ? true
                                                                      : false;
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Rds, 'Rns, 'IAddSub";
  const char *form_cmp = "'Rns, 'IAddSub";
  const char *form_mov = "'Rds, 'Rns";

  switch (form_hash_) {
    case "add_32_addsub_imm"_h:
    case "add_64_addsub_imm"_h:
      if (stack_op) {
        mnemonic = "mov";
        form = form_mov;
      }
      break;
    case "adds_32s_addsub_imm"_h:
    case "adds_64s_addsub_imm"_h:
      if (rd_is_zr) {
        mnemonic = "cmn";
        form = form_cmp;
      }
      break;
    case "subs_32s_addsub_imm"_h:
    case "subs_64s_addsub_imm"_h:
      if (rd_is_zr) {
        mnemonic = "cmp";
        form = form_cmp;
      }
      break;
  }
  Format(instr, mnemonic, form);
}


void Disassembler::VisitAddSubShifted(const Instruction *instr) {
  bool rd_is_zr = RdIsZROrSP(instr);
  bool rn_is_zr = RnIsZROrSP(instr);
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Rd, 'Rn, 'Rm'NDP";
  const char *form_cmp = "'Rn, 'Rm'NDP";
  const char *form_neg = "'Rd, 'Rm'NDP";

  if (instr->GetShiftDP() == ROR) {
    // Add/sub/adds/subs don't allow ROR as a shift mode.
    VisitUnallocated(instr);
    return;
  }

  switch (form_hash_) {
    case "adds_32_addsub_shift"_h:
    case "adds_64_addsub_shift"_h:
      if (rd_is_zr) {
        mnemonic = "cmn";
        form = form_cmp;
      }
      break;
    case "sub_32_addsub_shift"_h:
    case "sub_64_addsub_shift"_h:
      if (rn_is_zr) {
        mnemonic = "neg";
        form = form_neg;
      }
      break;
    case "subs_32_addsub_shift"_h:
    case "subs_64_addsub_shift"_h:
      if (rd_is_zr) {
        mnemonic = "cmp";
        form = form_cmp;
      } else if (rn_is_zr) {
        mnemonic = "negs";
        form = form_neg;
      }
  }
  Format(instr, mnemonic, form);
}


void Disassembler::VisitAddSubExtended(const Instruction *instr) {
  bool rd_is_zr = RdIsZROrSP(instr);
  const char *mnemonic = "";
  Extend mode = static_cast<Extend>(instr->GetExtendMode());
  const char *form = ((mode == UXTX) || (mode == SXTX)) ? "'Rds, 'Rns, 'Xm'Ext"
                                                        : "'Rds, 'Rns, 'Wm'Ext";
  const char *form_cmp =
      ((mode == UXTX) || (mode == SXTX)) ? "'Rns, 'Xm'Ext" : "'Rns, 'Wm'Ext";

  switch (instr->Mask(AddSubExtendedMask)) {
    case ADD_w_ext:
    case ADD_x_ext:
      mnemonic = "add";
      break;
    case ADDS_w_ext:
    case ADDS_x_ext: {
      mnemonic = "adds";
      if (rd_is_zr) {
        mnemonic = "cmn";
        form = form_cmp;
      }
      break;
    }
    case SUB_w_ext:
    case SUB_x_ext:
      mnemonic = "sub";
      break;
    case SUBS_w_ext:
    case SUBS_x_ext: {
      mnemonic = "subs";
      if (rd_is_zr) {
        mnemonic = "cmp";
        form = form_cmp;
      }
      break;
    }
    default:
      VIXL_UNREACHABLE();
  }
  Format(instr, mnemonic, form);
}


void Disassembler::VisitAddSubWithCarry(const Instruction *instr) {
  bool rn_is_zr = RnIsZROrSP(instr);
  const char *mnemonic = "";
  const char *form = "'Rd, 'Rn, 'Rm";
  const char *form_neg = "'Rd, 'Rm";

  switch (instr->Mask(AddSubWithCarryMask)) {
    case ADC_w:
    case ADC_x:
      mnemonic = "adc";
      break;
    case ADCS_w:
    case ADCS_x:
      mnemonic = "adcs";
      break;
    case SBC_w:
    case SBC_x: {
      mnemonic = "sbc";
      if (rn_is_zr) {
        mnemonic = "ngc";
        form = form_neg;
      }
      break;
    }
    case SBCS_w:
    case SBCS_x: {
      mnemonic = "sbcs";
      if (rn_is_zr) {
        mnemonic = "ngcs";
        form = form_neg;
      }
      break;
    }
    default:
      VIXL_UNREACHABLE();
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitLogicalImmediate(const Instruction *instr) {
  bool rd_is_zr = RdIsZROrSP(instr);
  bool rn_is_zr = RnIsZROrSP(instr);
  const char *mnemonic = "";
  const char *form = "'Rds, 'Rn, 'ITri";

  if (instr->GetImmLogical() == 0) {
    // The immediate encoded in the instruction is not in the expected format.
    Format(instr, "unallocated", "(LogicalImmediate)");
    return;
  }

  switch (instr->Mask(LogicalImmediateMask)) {
    case AND_w_imm:
    case AND_x_imm:
      mnemonic = "and";
      break;
    case ORR_w_imm:
    case ORR_x_imm: {
      mnemonic = "orr";
      unsigned reg_size =
          (instr->GetSixtyFourBits() == 1) ? kXRegSize : kWRegSize;
      if (rn_is_zr && !IsMovzMovnImm(reg_size, instr->GetImmLogical())) {
        mnemonic = "mov";
        form = "'Rds, 'ITri";
      }
      break;
    }
    case EOR_w_imm:
    case EOR_x_imm:
      mnemonic = "eor";
      break;
    case ANDS_w_imm:
    case ANDS_x_imm: {
      mnemonic = "ands";
      if (rd_is_zr) {
        mnemonic = "tst";
        form = "'Rn, 'ITri";
      }
      break;
    }
    default:
      VIXL_UNREACHABLE();
  }
  Format(instr, mnemonic, form);
}


bool Disassembler::IsMovzMovnImm(unsigned reg_size, uint64_t value) {
  VIXL_ASSERT((reg_size == kXRegSize) ||
              ((reg_size == kWRegSize) && (value <= 0xffffffff)));

  // Test for movz: 16 bits set at positions 0, 16, 32 or 48.
  if (((value & UINT64_C(0xffffffffffff0000)) == 0) ||
      ((value & UINT64_C(0xffffffff0000ffff)) == 0) ||
      ((value & UINT64_C(0xffff0000ffffffff)) == 0) ||
      ((value & UINT64_C(0x0000ffffffffffff)) == 0)) {
    return true;
  }

  // Test for movn: NOT(16 bits set at positions 0, 16, 32 or 48).
  if ((reg_size == kXRegSize) &&
      (((~value & UINT64_C(0xffffffffffff0000)) == 0) ||
       ((~value & UINT64_C(0xffffffff0000ffff)) == 0) ||
       ((~value & UINT64_C(0xffff0000ffffffff)) == 0) ||
       ((~value & UINT64_C(0x0000ffffffffffff)) == 0))) {
    return true;
  }
  if ((reg_size == kWRegSize) && (((value & 0xffff0000) == 0xffff0000) ||
                                  ((value & 0x0000ffff) == 0x0000ffff))) {
    return true;
  }
  return false;
}


void Disassembler::VisitLogicalShifted(const Instruction *instr) {
  bool rd_is_zr = RdIsZROrSP(instr);
  bool rn_is_zr = RnIsZROrSP(instr);
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Rd, 'Rn, 'Rm'NLo";

  switch (form_hash_) {
    case "ands_32_log_shift"_h:
    case "ands_64_log_shift"_h:
      if (rd_is_zr) {
        mnemonic = "tst";
        form = "'Rn, 'Rm'NLo";
      }
      break;
    case "orr_32_log_shift"_h:
    case "orr_64_log_shift"_h:
      if (rn_is_zr && (instr->GetImmDPShift() == 0) &&
          (instr->GetShiftDP() == LSL)) {
        mnemonic = "mov";
        form = "'Rd, 'Rm";
      }
      break;
    case "orn_32_log_shift"_h:
    case "orn_64_log_shift"_h:
      if (rn_is_zr) {
        mnemonic = "mvn";
        form = "'Rd, 'Rm'NLo";
      }
      break;
  }

  Format(instr, mnemonic, form);
}

void Disassembler::VisitConditionalSelect(const Instruction *instr) {
  bool rnm_is_zr = (RnIsZROrSP(instr) && RmIsZROrSP(instr));
  bool rn_is_rm = (instr->GetRn() == instr->GetRm());
  const char *mnemonic = "";
  const char *form = "'Rd, 'Rn, 'Rm, 'Cond";
  const char *form_test = "'Rd, 'CInv";
  const char *form_update = "'Rd, 'Rn, 'CInv";

  Condition cond = static_cast<Condition>(instr->GetCondition());
  bool invertible_cond = (cond != al) && (cond != nv);

  switch (instr->Mask(ConditionalSelectMask)) {
    case CSEL_w:
    case CSEL_x:
      mnemonic = "csel";
      break;
    case CSINC_w:
    case CSINC_x: {
      mnemonic = "csinc";
      if (rnm_is_zr && invertible_cond) {
        mnemonic = "cset";
        form = form_test;
      } else if (rn_is_rm && invertible_cond) {
        mnemonic = "cinc";
        form = form_update;
      }
      break;
    }
    case CSINV_w:
    case CSINV_x: {
      mnemonic = "csinv";
      if (rnm_is_zr && invertible_cond) {
        mnemonic = "csetm";
        form = form_test;
      } else if (rn_is_rm && invertible_cond) {
        mnemonic = "cinv";
        form = form_update;
      }
      break;
    }
    case CSNEG_w:
    case CSNEG_x: {
      mnemonic = "csneg";
      if (rn_is_rm && invertible_cond) {
        mnemonic = "cneg";
        form = form_update;
      }
      break;
    }
    default:
      VIXL_UNREACHABLE();
  }
  Format(instr, mnemonic, form);
}


void Disassembler::VisitBitfield(const Instruction *instr) {
  unsigned s = instr->GetImmS();
  unsigned r = instr->GetImmR();
  unsigned rd_size_minus_1 =
      ((instr->GetSixtyFourBits() == 1) ? kXRegSize : kWRegSize) - 1;
  const char *mnemonic = "";
  const char *form = "";
  const char *form_shift_right = "'Rd, 'Rn, 'IBr";
  const char *form_extend = "'Rd, 'Wn";
  const char *form_bfiz = "'Rd, 'Rn, 'IBZ-r, 'IBs+1";
  const char *form_bfc = "'Rd, 'IBZ-r, 'IBs+1";
  const char *form_bfx = "'Rd, 'Rn, 'IBr, 'IBs-r+1";
  const char *form_lsl = "'Rd, 'Rn, 'IBZ-r";

  if (instr->GetSixtyFourBits() != instr->GetBitN()) {
    VisitUnallocated(instr);
    return;
  }

  if ((instr->GetSixtyFourBits() == 0) && ((s > 31) || (r > 31))) {
    VisitUnallocated(instr);
    return;
  }

  switch (instr->Mask(BitfieldMask)) {
    case SBFM_w:
    case SBFM_x: {
      mnemonic = "sbfx";
      form = form_bfx;
      if (r == 0) {
        form = form_extend;
        if (s == 7) {
          mnemonic = "sxtb";
        } else if (s == 15) {
          mnemonic = "sxth";
        } else if ((s == 31) && (instr->GetSixtyFourBits() == 1)) {
          mnemonic = "sxtw";
        } else {
          form = form_bfx;
        }
      } else if (s == rd_size_minus_1) {
        mnemonic = "asr";
        form = form_shift_right;
      } else if (s < r) {
        mnemonic = "sbfiz";
        form = form_bfiz;
      }
      break;
    }
    case UBFM_w:
    case UBFM_x: {
      mnemonic = "ubfx";
      form = form_bfx;
      if (r == 0) {
        form = form_extend;
        if (s == 7) {
          mnemonic = "uxtb";
        } else if (s == 15) {
          mnemonic = "uxth";
        } else {
          form = form_bfx;
        }
      }
      if (s == rd_size_minus_1) {
        mnemonic = "lsr";
        form = form_shift_right;
      } else if (r == s + 1) {
        mnemonic = "lsl";
        form = form_lsl;
      } else if (s < r) {
        mnemonic = "ubfiz";
        form = form_bfiz;
      }
      break;
    }
    case BFM_w:
    case BFM_x: {
      mnemonic = "bfxil";
      form = form_bfx;
      if (s < r) {
        if (instr->GetRn() == kZeroRegCode) {
          mnemonic = "bfc";
          form = form_bfc;
        } else {
          mnemonic = "bfi";
          form = form_bfiz;
        }
      }
    }
  }
  Format(instr, mnemonic, form);
}


void Disassembler::VisitExtract(const Instruction *instr) {
  const char *mnemonic = "";
  const char *form = "'Rd, 'Rn, 'Rm, 'IExtract";

  switch (instr->Mask(ExtractMask)) {
    case EXTR_w:
    case EXTR_x: {
      if (instr->GetRn() == instr->GetRm()) {
        mnemonic = "ror";
        form = "'Rd, 'Rn, 'IExtract";
      } else {
        mnemonic = "extr";
      }
      break;
    }
    default:
      VIXL_UNREACHABLE();
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitConditionalBranch(const Instruction *instr) {
  // We can't use the mnemonic directly here, as there's no space between it and
  // the condition. Assert that we have the correct mnemonic, then use "b"
  // explicitly for formatting the output.
  if (form_hash_ == "bc_only_condbranch"_h) {
    Format(instr, "bc.'CBrn", "'TImmCond");
  } else {
    VIXL_ASSERT(form_hash_ == "b_only_condbranch"_h);
    Format(instr, "b.'CBrn", "'TImmCond");
  }
}


void Disassembler::VisitUnconditionalBranchToRegister(
    const Instruction *instr) {
  const char *form = "'Xn";

  switch (form_hash_) {
    case "ret_64r_branch_reg"_h:
      form = "'(0905=30?:'Xn)";
      break;
    case "retaa_64e_branch_reg"_h:
    case "retab_64e_branch_reg"_h:
      form = "";
      break;
    case "braa_64p_branch_reg"_h:
    case "brab_64p_branch_reg"_h:
    case "blraa_64p_branch_reg"_h:
    case "blrab_64p_branch_reg"_h:
      form = "'Xn, 'Xds";
      break;
  }

  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitDataProcessing2Source(const Instruction *instr) {
  std::string mnemonic = mnemonic_;
  const char *form = "'Rd, 'Rn, 'Rm";

  switch (form_hash_) {
    case "asrv_32_dp_2src"_h:
    case "asrv_64_dp_2src"_h:
    case "lslv_32_dp_2src"_h:
    case "lslv_64_dp_2src"_h:
    case "lsrv_32_dp_2src"_h:
    case "lsrv_64_dp_2src"_h:
    case "rorv_32_dp_2src"_h:
    case "rorv_64_dp_2src"_h:
      // Drop the last 'v' character.
      VIXL_ASSERT(mnemonic[3] == 'v');
      mnemonic.pop_back();
      break;
    default:
      VIXL_UNREACHABLE();
  }
  Format(instr, mnemonic.c_str(), form);
}


void Disassembler::VisitDataProcessing3Source(const Instruction *instr) {
  bool ra_is_zr = RaIsZROrSP(instr);
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Xd, 'Wn, 'Wm, 'Xa";
  const char *form_rrr = "'Rd, 'Rn, 'Rm";
  const char *form_rrrr = "'Rd, 'Rn, 'Rm, 'Ra";
  const char *form_xww = "'Xd, 'Wn, 'Wm";
  const char *form_xxx = "'Xd, 'Xn, 'Xm";

  switch (form_hash_) {
    case "madd_32a_dp_3src"_h:
    case "madd_64a_dp_3src"_h:
      form = form_rrrr;
      if (ra_is_zr) {
        mnemonic = "mul";
        form = form_rrr;
      }
      break;
    case "msub_32a_dp_3src"_h:
    case "msub_64a_dp_3src"_h:
      form = form_rrrr;
      if (ra_is_zr) {
        mnemonic = "mneg";
        form = form_rrr;
      }
      break;
    case "smaddl_64wa_dp_3src"_h:
      if (ra_is_zr) {
        mnemonic = "smull";
        form = form_xww;
      }
      break;
    case "smsubl_64wa_dp_3src"_h:
      if (ra_is_zr) {
        mnemonic = "smnegl";
        form = form_xww;
      }
      break;
    case "umaddl_64wa_dp_3src"_h:
      if (ra_is_zr) {
        mnemonic = "umull";
        form = form_xww;
      }
      break;
    case "umsubl_64wa_dp_3src"_h:
      if (ra_is_zr) {
        mnemonic = "umnegl";
        form = form_xww;
      }
      break;
    case "smulh_64_dp_3src"_h:
    case "umulh_64_dp_3src"_h:
      form = form_xxx;
      break;
  }

  Format(instr, mnemonic, form);
}

void Disassembler::VisitMoveWideImmediate(const Instruction *instr) {
  const char *mnemonic = "";
  const char *form = "'Rd, 'IMoveImm";

  // Print the shift separately for movk, to make it clear which half word will
  // be overwritten. Movn and movz print the computed immediate, which includes
  // shift calculation.
  switch (instr->Mask(MoveWideImmediateMask)) {
    case MOVN_w:
    case MOVN_x:
      if ((instr->GetImmMoveWide()) || (instr->GetShiftMoveWide() == 0)) {
        if ((instr->GetSixtyFourBits() == 0) &&
            (instr->GetImmMoveWide() == 0xffff)) {
          mnemonic = "movn";
        } else {
          mnemonic = "mov";
          form = "'Rd, 'IMoveNeg";
        }
      } else {
        mnemonic = "movn";
      }
      break;
    case MOVZ_w:
    case MOVZ_x:
      if ((instr->GetImmMoveWide()) || (instr->GetShiftMoveWide() == 0))
        mnemonic = "mov";
      else
        mnemonic = "movz";
      break;
    case MOVK_w:
    case MOVK_x:
      mnemonic = "movk";
      form = "'Rd, 'IMoveLSL";
      break;
    default:
      VIXL_UNREACHABLE();
  }
  Format(instr, mnemonic, form);
}


#define LOAD_STORE_LIST(V) \
  V(STRB_w, "'Wt")         \
  V(STRH_w, "'Wt")         \
  V(STR_w, "'Wt")          \
  V(STR_x, "'Xt")          \
  V(LDRB_w, "'Wt")         \
  V(LDRH_w, "'Wt")         \
  V(LDR_w, "'Wt")          \
  V(LDR_x, "'Xt")          \
  V(LDRSB_x, "'Xt")        \
  V(LDRSH_x, "'Xt")        \
  V(LDRSW_x, "'Xt")        \
  V(LDRSB_w, "'Wt")        \
  V(LDRSH_w, "'Wt")        \
  V(STR_b, "'Bt")          \
  V(STR_h, "'Ht")          \
  V(STR_s, "'St")          \
  V(STR_d, "'Dt")          \
  V(LDR_b, "'Bt")          \
  V(LDR_h, "'Ht")          \
  V(LDR_s, "'St")          \
  V(LDR_d, "'Dt")          \
  V(STR_q, "'Qt")          \
  V(LDR_q, "'Qt")

void Disassembler::VisitLoadStorePreIndex(const Instruction *instr) {
  const char *form = "(LoadStorePreIndex)";
  const char *suffix = ", ['Xns'ILSi]!";

  switch (instr->Mask(LoadStorePreIndexMask)) {
#define LS_PREINDEX(A, B) \
  case A##_pre:           \
    form = B;             \
    break;
    LOAD_STORE_LIST(LS_PREINDEX)
#undef LS_PREINDEX
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}


void Disassembler::VisitLoadStorePostIndex(const Instruction *instr) {
  const char *form = "(LoadStorePostIndex)";
  const char *suffix = ", ['Xns]'ILSi";

  switch (instr->Mask(LoadStorePostIndexMask)) {
#define LS_POSTINDEX(A, B) \
  case A##_post:           \
    form = B;              \
    break;
    LOAD_STORE_LIST(LS_POSTINDEX)
#undef LS_POSTINDEX
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}


void Disassembler::VisitLoadStoreUnsignedOffset(const Instruction *instr) {
  const char *form = "(LoadStoreUnsignedOffset)";
  const char *suffix = ", ['Xns'ILU]";

  switch (instr->Mask(LoadStoreUnsignedOffsetMask)) {
#define LS_UNSIGNEDOFFSET(A, B) \
  case A##_unsigned:            \
    form = B;                   \
    break;
    LOAD_STORE_LIST(LS_UNSIGNEDOFFSET)
#undef LS_UNSIGNEDOFFSET
    case PRFM_unsigned:
      form = "'prefOp";
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitLoadStoreRegisterOffset(const Instruction *instr) {
  const char *form = "(LoadStoreRegisterOffset)";
  const char *suffix = ", ['Xns, 'Offsetreg]";

  switch (instr->Mask(LoadStoreRegisterOffsetMask)) {
#define LS_REGISTEROFFSET(A, B) \
  case A##_reg:                 \
    form = B;                   \
    break;
    LOAD_STORE_LIST(LS_REGISTEROFFSET)
#undef LS_REGISTEROFFSET
    case PRFM_reg:
      form = "'prefOp";
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

#define LOAD_STORE_PAIR_LIST(V) \
  V(STP_w, "'Wt, 'Wt2", "2")    \
  V(LDP_w, "'Wt, 'Wt2", "2")    \
  V(LDPSW_x, "'Xt, 'Xt2", "2")  \
  V(STP_x, "'Xt, 'Xt2", "3")    \
  V(LDP_x, "'Xt, 'Xt2", "3")    \
  V(STP_s, "'St, 'St2", "2")    \
  V(LDP_s, "'St, 'St2", "2")    \
  V(STP_d, "'Dt, 'Dt2", "3")    \
  V(LDP_d, "'Dt, 'Dt2", "3")    \
  V(LDP_q, "'Qt, 'Qt2", "4")    \
  V(STP_q, "'Qt, 'Qt2", "4")

void Disassembler::VisitLoadStorePairPostIndex(const Instruction *instr) {
  const char *form = "(LoadStorePairPostIndex)";

  switch (instr->Mask(LoadStorePairPostIndexMask)) {
#define LSP_POSTINDEX(A, B, C)     \
  case A##_post:                   \
    form = B ", ['Xns]'ILP" C "i"; \
    break;
    LOAD_STORE_PAIR_LIST(LSP_POSTINDEX)
#undef LSP_POSTINDEX
  }
  FormatWithDecodedMnemonic(instr, form);
}


void Disassembler::VisitLoadStorePairPreIndex(const Instruction *instr) {
  const char *form = "(LoadStorePairPreIndex)";

  switch (instr->Mask(LoadStorePairPreIndexMask)) {
#define LSP_PREINDEX(A, B, C)       \
  case A##_pre:                     \
    form = B ", ['Xns'ILP" C "i]!"; \
    break;
    LOAD_STORE_PAIR_LIST(LSP_PREINDEX)
#undef LSP_PREINDEX
  }
  FormatWithDecodedMnemonic(instr, form);
}


void Disassembler::VisitLoadStorePairOffset(const Instruction *instr) {
  const char *form = "(LoadStorePairOffset)";

  switch (instr->Mask(LoadStorePairOffsetMask)) {
#define LSP_OFFSET(A, B, C)       \
  case A##_off:                   \
    form = B ", ['Xns'ILP" C "]"; \
    break;
    LOAD_STORE_PAIR_LIST(LSP_OFFSET)
#undef LSP_OFFSET
  }
  FormatWithDecodedMnemonic(instr, form);
}

// clang-format off
#define LOAD_STORE_EXCLUSIVE_LIST(V)   \
  V(STXRB_w,  "'Ws, 'Wt")              \
  V(STXRH_w,  "'Ws, 'Wt")              \
  V(STXR_w,   "'Ws, 'Wt")              \
  V(STXR_x,   "'Ws, 'Xt")              \
  V(LDXR_x,   "'Xt")                   \
  V(STXP_w,   "'Ws, 'Wt, 'Wt2")        \
  V(STXP_x,   "'Ws, 'Xt, 'Xt2")        \
  V(LDXP_w,   "'Wt, 'Wt2")             \
  V(LDXP_x,   "'Xt, 'Xt2")             \
  V(STLXRB_w, "'Ws, 'Wt")              \
  V(STLXRH_w, "'Ws, 'Wt")              \
  V(STLXR_w,  "'Ws, 'Wt")              \
  V(STLXR_x,  "'Ws, 'Xt")              \
  V(LDAXR_x,  "'Xt")                   \
  V(STLXP_w,  "'Ws, 'Wt, 'Wt2")        \
  V(STLXP_x,  "'Ws, 'Xt, 'Xt2")        \
  V(LDAXP_w,  "'Wt, 'Wt2")             \
  V(LDAXP_x,  "'Xt, 'Xt2")             \
  V(STLR_x,   "'Xt")                   \
  V(LDAR_x,   "'Xt")                   \
  V(STLLR_x,  "'Xt")                   \
  V(LDLAR_x,  "'Xt")                   \
  V(CAS_w,    "'Ws, 'Wt")              \
  V(CAS_x,    "'Xs, 'Xt")              \
  V(CASA_w,   "'Ws, 'Wt")              \
  V(CASA_x,   "'Xs, 'Xt")              \
  V(CASL_w,   "'Ws, 'Wt")              \
  V(CASL_x,   "'Xs, 'Xt")              \
  V(CASAL_w,  "'Ws, 'Wt")              \
  V(CASAL_x,  "'Xs, 'Xt")              \
  V(CASB,     "'Ws, 'Wt")              \
  V(CASAB,    "'Ws, 'Wt")              \
  V(CASLB,    "'Ws, 'Wt")              \
  V(CASALB,   "'Ws, 'Wt")              \
  V(CASH,     "'Ws, 'Wt")              \
  V(CASAH,    "'Ws, 'Wt")              \
  V(CASLH,    "'Ws, 'Wt")              \
  V(CASALH,   "'Ws, 'Wt")              \
  V(CASP_w,   "'Ws, 'Ws+, 'Wt, 'Wt+")  \
  V(CASP_x,   "'Xs, 'Xs+, 'Xt, 'Xt+")  \
  V(CASPA_w,  "'Ws, 'Ws+, 'Wt, 'Wt+")  \
  V(CASPA_x,  "'Xs, 'Xs+, 'Xt, 'Xt+")  \
  V(CASPL_w,  "'Ws, 'Ws+, 'Wt, 'Wt+")  \
  V(CASPL_x,  "'Xs, 'Xs+, 'Xt, 'Xt+")  \
  V(CASPAL_w, "'Ws, 'Ws+, 'Wt, 'Wt+")  \
  V(CASPAL_x, "'Xs, 'Xs+, 'Xt, 'Xt+")
// clang-format on


void Disassembler::VisitLoadStoreExclusive(const Instruction *instr) {
  const char *form = "'Wt";
  const char *suffix = ", ['Xns]";

  switch (instr->Mask(LoadStoreExclusiveMask)) {
#define LSX(A, B) \
  case A:         \
    form = B;     \
    break;
    LOAD_STORE_EXCLUSIVE_LIST(LSX)
#undef LSX
  }

  switch (instr->Mask(LoadStoreExclusiveMask)) {
    case CASP_w:
    case CASP_x:
    case CASPA_w:
    case CASPA_x:
    case CASPL_w:
    case CASPL_x:
    case CASPAL_w:
    case CASPAL_x:
      if ((instr->GetRs() % 2 == 1) || (instr->GetRt() % 2 == 1)) {
        VisitUnallocated(instr);
        return;
      }
      break;
  }

  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitAtomicMemory(const Instruction *instr) {
  const char *form = "'(3130=3?'Xs:'Ws), '(3130=3?'Xt:'Wt)";
  const char *suffix = ", ['Xns]";

  std::string mnemonic = mnemonic_;

  switch (form_hash_) {
    case "ldaprb_32l_memop"_h:
    case "ldaprh_32l_memop"_h:
    case "ldapr_32l_memop"_h:
      form = "'Wt";
      break;
    case "ldapr_64l_memop"_h:
      form = "'Xt";
      break;
    default:
      // Zero register implies a store instruction.
      if (instr->GetRt() == kZeroRegCode) {
        mnemonic.replace(0, 2, "st");
        form = "'(3130=3?'Xs:'Ws)";
      }
  }
  Format(instr, mnemonic.c_str(), form, suffix);
}

void Disassembler::VisitFPDataProcessing1Source(const Instruction *instr) {
  const char *form = "'Fd, 'Fn";
  switch (form_hash_) {
    case "fcvt_ds_floatdp1"_h:
      form = "'Dd, 'Sn";
      break;
    case "fcvt_sd_floatdp1"_h:
      form = "'Sd, 'Dn";
      break;
    case "fcvt_hs_floatdp1"_h:
      form = "'Hd, 'Sn";
      break;
    case "fcvt_sh_floatdp1"_h:
      form = "'Sd, 'Hn";
      break;
    case "fcvt_dh_floatdp1"_h:
      form = "'Dd, 'Hn";
      break;
    case "fcvt_hd_floatdp1"_h:
      form = "'Hd, 'Dn";
      break;
    case "bfcvt_bs_floatdp1"_h:
      form = "'Hd, 'Sn";
      break;
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitFPImmediate(const Instruction *instr) {
  const char *form = "'Hd";
  const char *suffix = ", 'IFP";
  switch (form_hash_) {
    case "fmov_s_floatimm"_h:
      form = "'Sd";
      break;
    case "fmov_d_floatimm"_h:
      form = "'Dd";
      break;
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSystem(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "";
  const char *suffix = NULL;

  switch (form_hash_) {
    case "clrex_bn_barriers"_h:
      form = "'(1108=15?:'IX)";
      break;
    case "mrs_rs_systemmove"_h:
      form = "'Xt, 'IY";
      break;
    case "msr_sr_systemmove"_h:
      form = "'IY, 'Xt";
      break;
    case "bti_hb_hints"_h:
      switch (instr->ExtractBits(7, 6)) {
        case 0:
          form = "";
          break;
        case 1:
          form = "c";
          break;
        case 2:
          form = "j";
          break;
        case 3:
          form = "jc";
          break;
      }
      break;
    case "chkfeat_hf_hints"_h:
      mnemonic = "chkfeat";
      form = "x16";
      break;
    case "hint_hm_hints"_h:
      form = "'IH";
      break;
    case Hash("dmb_bo_barriers"):
      form = "'M";
      break;
    case Hash("dsb_bo_barriers"): {
      int crm = instr->GetCRm();
      if (crm == 0) {
        mnemonic = "ssbb";
        form = "";
      } else if (crm == 4) {
        mnemonic = "pssbb";
        form = "";
      } else {
        form = "'M";
      }
      break;
    }
    case Hash("sys_cr_systeminstrs"): {
      const std::map<uint32_t, const char *> dcop = {
          {IVAU, "ivau"},
          {CVAC, "cvac"},
          {CVAU, "cvau"},
          {CVAP, "cvap"},
          {CVADP, "cvadp"},
          {CIVAC, "civac"},
          {ZVA, "zva"},
          {GVA, "gva"},
          {GZVA, "gzva"},
          {CGVAC, "cgvac"},
          {CGDVAC, "cgdvac"},
          {CGVAP, "cgvap"},
          {CGDVAP, "cgdvap"},
          {CIGVAC, "cigvac"},
          {CIGDVAC, "cigdvac"},
      };

      uint32_t sysop = instr->GetSysOp();
      if (dcop.count(sysop)) {
        if (sysop == IVAU) {
          mnemonic = "ic";
        } else {
          mnemonic = "dc";
        }
        form = dcop.at(sysop);
        suffix = ", 'Xt";
      } else if (sysop == GCSSS1) {
        mnemonic = "gcsss1";
        form = "'Xt";
      } else if (sysop == GCSPUSHM) {
        mnemonic = "gcspushm";
        form = "'Xt";
      } else {
        mnemonic = "sys";
        form = "'G1, 'Kn, 'Km, 'G2";
        if (instr->GetRt() < 31) {
          suffix = ", 'Xt";
        }
      }
      break;
    }
    case "sysl_rc_systeminstrs"_h:
      uint32_t sysop = instr->GetSysOp();
      if (sysop == GCSPOPM) {
        mnemonic = "gcspopm";
        form = (instr->GetRt() == 31) ? "" : "'Xt";
      } else if (sysop == GCSSS2) {
        mnemonic = "gcsss2";
        form = "'Xt";
      }
      break;
  }
  Format(instr, mnemonic, form, suffix);
}

void Disassembler::DisassembleNEON2RegAddlp(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s";

  static const NEONFormatMap map_lp_ta =
      {{23, 22, 30}, {NF_4H, NF_8H, NF_2S, NF_4S, NF_1D, NF_2D}};
  NEONFormatDecoder nfd(instr);
  nfd.SetFormatMap(0, &map_lp_ta);
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEON2RegCompare(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, #0";
  NEONFormatDecoder nfd(instr);
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEON2RegFPCompare(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, #0.0";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::FPFormatMap());
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEON2RegFPConvert(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s";
  static const NEONFormatMap map_cvt_ta = {{22}, {NF_4S, NF_2D}};

  static const NEONFormatMap map_cvt_tb = {{22, 30},
                                           {NF_4H, NF_8H, NF_2S, NF_4S}};
  NEONFormatDecoder nfd(instr, &map_cvt_tb, &map_cvt_ta);
  VectorFormat vform_dst = nfd.GetVectorFormat(0);
  switch (form_hash_) {
    case "fcvtl_asimdmisc_l"_h:
      nfd.SetFormatMaps(&map_cvt_ta, &map_cvt_tb);
      break;
    case "fcvtxn_asimdmisc_n"_h:
      if ((vform_dst != kFormat2S) && (vform_dst != kFormat4S)) {
        mnemonic = NULL;
      }
      break;
    case "bfcvtn_asimdmisc_4s"_h:
      form = "'Vd.'?30:84h, 'Vn.4s";
      break;
  }
  Format(instr, nfd.Mnemonic(mnemonic), nfd.Substitute(form));
}

void Disassembler::DisassembleNEON2RegFP(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::FPFormatMap());
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEON2RegLogical(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LogicalFormatMap());
  if (form_hash_ == "not_asimdmisc_r"_h) {
    mnemonic = "mvn";
  }
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEON2RegExtract(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s";
  const char *suffix = NULL;
  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::IntegerFormatMap(),
                        NEONFormatDecoder::LongIntegerFormatMap());

  if (form_hash_ == "shll_asimdmisc_s"_h) {
    nfd.SetFormatMaps(nfd.LongIntegerFormatMap(), nfd.IntegerFormatMap());
    switch (instr->GetNEONSize()) {
      case 0:
        suffix = ", #8";
        break;
      case 1:
        suffix = ", #16";
        break;
      case 2:
        suffix = ", #32";
        break;
    }
  }
  Format(instr, nfd.Mnemonic(mnemonic), nfd.Substitute(form), suffix);
}

void Disassembler::VisitNEON2RegMisc(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s";
  NEONFormatDecoder nfd(instr);

  VectorFormat vform_dst = nfd.GetVectorFormat(0);
  if (vform_dst != kFormatUndefined) {
    uint32_t ls_dst = LaneSizeInBitsFromFormat(vform_dst);
    switch (form_hash_) {
      case "cnt_asimdmisc_r"_h:
      case "rev16_asimdmisc_r"_h:
        if (ls_dst != kBRegSize) {
          mnemonic = NULL;
        }
        break;
      case "rev32_asimdmisc_r"_h:
        if ((ls_dst == kDRegSize) || (ls_dst == kSRegSize)) {
          mnemonic = NULL;
        }
        break;
      case "urecpe_asimdmisc_r"_h:
      case "ursqrte_asimdmisc_r"_h:
        // For urecpe and ursqrte, only S-sized elements are supported. The MSB
        // of the size field is always set by the instruction (0b1x) so we need
        // only check and discard D-sized elements here.
        VIXL_ASSERT((ls_dst == kSRegSize) || (ls_dst == kDRegSize));
        VIXL_FALLTHROUGH();
      case "clz_asimdmisc_r"_h:
      case "cls_asimdmisc_r"_h:
      case "rev64_asimdmisc_r"_h:
        if (ls_dst == kDRegSize) {
          mnemonic = NULL;
        }
        break;
    }
  }

  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEON3SameLogical(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LogicalFormatMap());

  switch (form_hash_) {
    case "orr_asimdsame_only"_h:
      if (instr->GetRm() == instr->GetRn()) {
        mnemonic = "mov";
        form = "'Vd.%s, 'Vn.%s";
      }
      break;
    case "pmul_asimdsame_only"_h:
      if (instr->GetNEONSize() != 0) {
        mnemonic = NULL;
      }
  }
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEON3SameNoD(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s";
  static const NEONFormatMap map =
      {{23, 22, 30},
       {NF_8B, NF_16B, NF_4H, NF_8H, NF_2S, NF_4S, NF_UNDEF, NF_UNDEF}};
  NEONFormatDecoder nfd(instr, &map);
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::VisitNEON3Same(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s";
  NEONFormatDecoder nfd(instr);

  if (instr->Mask(NEON3SameFPFMask) == NEON3SameFPFixed) {
    nfd.SetFormatMaps(nfd.FPFormatMap());
  }

  VectorFormat vform_dst = nfd.GetVectorFormat(0);
  if (vform_dst != kFormatUndefined) {
    uint32_t ls_dst = LaneSizeInBitsFromFormat(vform_dst);
    switch (form_hash_) {
      case "sqdmulh_asimdsame_only"_h:
      case "sqrdmulh_asimdsame_only"_h:
        if ((ls_dst == kBRegSize) || (ls_dst == kDRegSize)) {
          mnemonic = NULL;
        }
        break;
    }
  }
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::VisitNEON3SameFP16(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s";
  NEONFormatDecoder nfd(instr);
  nfd.SetFormatMaps(nfd.FP16FormatMap());
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::VisitNEON3SameExtra(const Instruction *instr) {
  static const NEONFormatMap map_dot =
      {{23, 22, 30}, {NF_UNDEF, NF_UNDEF, NF_UNDEF, NF_UNDEF, NF_2S, NF_4S}};
  static const NEONFormatMap map_fc =
      {{23, 22, 30},
       {NF_UNDEF, NF_UNDEF, NF_4H, NF_8H, NF_2S, NF_4S, NF_UNDEF, NF_2D}};
  static const NEONFormatMap map_rdm =
      {{23, 22, 30}, {NF_UNDEF, NF_UNDEF, NF_4H, NF_8H, NF_2S, NF_4S}};

  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s";
  const char *suffix = NULL;

  NEONFormatDecoder nfd(instr, &map_fc);

  switch (form_hash_) {
    case "fcmla_asimdsame2_c"_h:
      suffix = ", #'u1211*90";
      break;
    case "fcadd_asimdsame2_c"_h:
      // Bit 10 is always set, so this gives 90 * 1 or 3.
      suffix = ", #'u1212:1010*90";
      break;
    case "sdot_asimdsame2_d"_h:
    case "udot_asimdsame2_d"_h:
    case "usdot_asimdsame2_d"_h:
      nfd.SetFormatMaps(nfd.LogicalFormatMap());
      nfd.SetFormatMap(0, &map_dot);
      break;
    default:
      nfd.SetFormatMaps(&map_rdm);
      break;
  }

  Format(instr, mnemonic, nfd.Substitute(form), suffix);
}

void Disassembler::VisitNEON3Different(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s";

  NEONFormatDecoder nfd(instr);
  nfd.SetFormatMap(0, nfd.LongIntegerFormatMap());

  switch (form_hash_) {
    case "saddw_asimddiff_w"_h:
    case "ssubw_asimddiff_w"_h:
    case "uaddw_asimddiff_w"_h:
    case "usubw_asimddiff_w"_h:
      nfd.SetFormatMap(1, nfd.LongIntegerFormatMap());
      break;
    case "addhn_asimddiff_n"_h:
    case "raddhn_asimddiff_n"_h:
    case "rsubhn_asimddiff_n"_h:
    case "subhn_asimddiff_n"_h:
      nfd.SetFormatMaps(nfd.LongIntegerFormatMap());
      nfd.SetFormatMap(0, nfd.IntegerFormatMap());
      break;
    case "sqdmlal_asimddiff_l"_h:
    case "sqdmlsl_asimddiff_l"_h:
    case "sqdmull_asimddiff_l"_h:
      if (nfd.GetVectorFormat(0) == kFormat8H) {
        mnemonic = NULL;
      }
      break;
  }
  Format(instr, nfd.Mnemonic(mnemonic), nfd.Substitute(form));
}

void Disassembler::DisassembleNEONPolynomialMul(const Instruction *instr) {
  const char *mnemonic = instr->ExtractBit(30) ? "pmull2" : "pmull";
  const char *form = NULL;
  int size = instr->ExtractBits(23, 22);
  if (size == 0) {
    // Bits 30:27 of the instruction are x001, where x is the Q bit. Map
    // this to "8" and "16" by adding 7.
    form = "'Vd.8h, 'Vn.'u3127+7b, 'Vm.'u3127+7b";
  } else if (size == 3) {
    form = "'Vd.1q, 'Vn.'?30:21d, 'Vm.'?30:21d";
  } else {
    mnemonic = NULL;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::DisassembleNEONFPAcrossLanes(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Sd, 'Vn.4s";
  if ((instr->GetNEONQ() == 0) || (instr->ExtractBit(22) == 1)) {
    mnemonic = NULL;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitNEONAcrossLanes(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, 'Vn.%s";

  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::ScalarFormatMap(),
                        NEONFormatDecoder::IntegerFormatMap());

  switch (form_hash_) {
    case "saddlv_asimdall_only"_h:
    case "uaddlv_asimdall_only"_h:
      nfd.SetFormatMap(0, nfd.LongScalarFormatMap());
  }

  VectorFormat vform_src = nfd.GetVectorFormat(1);
  if ((vform_src == kFormat2S) || (vform_src == kFormat2D)) {
    mnemonic = NULL;
  }

  Format(instr,
         mnemonic,
         nfd.Substitute(form,
                        NEONFormatDecoder::kPlaceholder,
                        NEONFormatDecoder::kFormat));
}

void Disassembler::VisitNEONByIndexedElement(const Instruction *instr) {
  const char *form = "'Vd.%s, 'Vn.%s, 'Vf.%s['IVByElemIndex]";
  static const NEONFormatMap map_v =
      {{23, 22, 30},
       {NF_UNDEF, NF_UNDEF, NF_4H, NF_8H, NF_2S, NF_4S, NF_UNDEF, NF_UNDEF}};
  static const NEONFormatMap map_s = {{23, 22},
                                      {NF_UNDEF, NF_H, NF_S, NF_UNDEF}};
  NEONFormatDecoder nfd(instr, &map_v, &map_v, &map_s);
  Format(instr, mnemonic_.c_str(), nfd.Substitute(form));
}

void Disassembler::DisassembleNEONMulByElementLong(const Instruction *instr) {
  const char *form = "'Vd.%s, 'Vn.%s, 'Vf.%s['IVByElemIndex]";
  // TODO: Disallow undefined element types for this instruction.
  static const NEONFormatMap map_ta = {{23, 22}, {NF_UNDEF, NF_4S, NF_2D}};
  NEONFormatDecoder nfd(instr,
                        &map_ta,
                        NEONFormatDecoder::IntegerFormatMap(),
                        NEONFormatDecoder::ScalarFormatMap());
  Format(instr, nfd.Mnemonic(mnemonic_.c_str()), nfd.Substitute(form));
}

void Disassembler::DisassembleNEONDotProdByElement(const Instruction *instr) {
  const char *form = "'Vd.'?30:42s, 'Vn.'(3030?1)'?30:68b, 'Vm.4b['u1111:2121]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::DisassembleNEONFPMulByElement(const Instruction *instr) {
  const char *form = "'Vd.%s, 'Vn.%s, 'Vf.%s['IVByElemIndex]";
  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::FPFormatMap(),
                        NEONFormatDecoder::FPFormatMap(),
                        NEONFormatDecoder::FPScalarFormatMap());
  Format(instr, mnemonic_.c_str(), nfd.Substitute(form));
}

void Disassembler::DisassembleNEONComplexMulByElement(
    const Instruction *instr) {
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s['IVByElemIndexRot], #'u1413*90";
  // TODO: Disallow undefined element types for this instruction.
  static const NEONFormatMap map_cn =
      {{23, 22, 30},
       {NF_UNDEF, NF_UNDEF, NF_4H, NF_8H, NF_UNDEF, NF_4S, NF_UNDEF, NF_UNDEF}};
  NEONFormatDecoder nfd(instr,
                        &map_cn,
                        &map_cn,
                        NEONFormatDecoder::ScalarFormatMap());
  Format(instr, mnemonic_.c_str(), nfd.Substitute(form));
}

void Disassembler::VisitNEONCopy(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "(NEONCopy)";

  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::TriangularFormatMap(),
                        NEONFormatDecoder::TriangularScalarFormatMap());

  switch (form_hash_) {
    case "ins_asimdins_iv_v"_h:
      mnemonic = "mov";
      nfd.SetFormatMap(0, nfd.TriangularScalarFormatMap());
      form = "'Vd.%s['IVInsIndex1], 'Vn.%s['IVInsIndex2]";
      break;
    case "ins_asimdins_ir_r"_h:
      mnemonic = "mov";
      nfd.SetFormatMap(0, nfd.TriangularScalarFormatMap());
      if (nfd.GetVectorFormat() == kFormatD) {
        form = "'Vd.%s['IVInsIndex1], 'Xn";
      } else {
        form = "'Vd.%s['IVInsIndex1], 'Wn";
      }
      break;
    case "umov_asimdins_w_w"_h:
    case "umov_asimdins_x_x"_h:
      if (instr->Mask(NEON_Q) || ((instr->GetImmNEON5() & 7) == 4)) {
        mnemonic = "mov";
      }
      nfd.SetFormatMap(0, nfd.TriangularScalarFormatMap());
      if (nfd.GetVectorFormat() == kFormatD) {
        form = "'Xd, 'Vn.%s['IVInsIndex1]";
      } else {
        form = "'Wd, 'Vn.%s['IVInsIndex1]";
      }
      break;
    case "smov_asimdins_w_w"_h:
    case "smov_asimdins_x_x"_h: {
      nfd.SetFormatMap(0, nfd.TriangularScalarFormatMap());
      VectorFormat vform = nfd.GetVectorFormat();
      if ((vform == kFormatD) ||
          ((vform == kFormatS) && (instr->ExtractBit(30) == 0))) {
        mnemonic = NULL;
      }
      form = "'R30d, 'Vn.%s['IVInsIndex1]";
      break;
    }
    case "dup_asimdins_dv_v"_h:
      form = "'Vd.%s, 'Vn.%s['IVInsIndex1]";
      break;
    case "dup_asimdins_dr_r"_h:
      if (nfd.GetVectorFormat() == kFormat2D) {
        form = "'Vd.%s, 'Xn";
      } else {
        form = "'Vd.%s, 'Wn";
      }
  }
  Format(instr, mnemonic, nfd.Substitute(form));
}


void Disassembler::VisitNEONExtract(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'Vm.%s, 'IVExtract";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LogicalFormatMap());
  if ((instr->GetImmNEONExt() > 7) && (instr->GetNEONQ() == 0)) {
    mnemonic = NULL;
  }
  Format(instr, mnemonic, nfd.Substitute(form));
}


void Disassembler::VisitNEONLoadStoreMultiStruct(const Instruction *instr) {
  const char *mnemonic = NULL;
  const char *form = NULL;
  const char *form_1v = "{'Vt.%s}, ['Xns]";
  const char *form_2v = "{'Vt.%s, 'Vt2.%s}, ['Xns]";
  const char *form_3v = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s}, ['Xns]";
  const char *form_4v = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s, 'Vt4.%s}, ['Xns]";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LoadStoreFormatMap());

  switch (instr->Mask(NEONLoadStoreMultiStructMask)) {
    case NEON_LD1_1v:
      mnemonic = "ld1";
      form = form_1v;
      break;
    case NEON_LD1_2v:
      mnemonic = "ld1";
      form = form_2v;
      break;
    case NEON_LD1_3v:
      mnemonic = "ld1";
      form = form_3v;
      break;
    case NEON_LD1_4v:
      mnemonic = "ld1";
      form = form_4v;
      break;
    case NEON_LD2:
      mnemonic = "ld2";
      form = form_2v;
      break;
    case NEON_LD3:
      mnemonic = "ld3";
      form = form_3v;
      break;
    case NEON_LD4:
      mnemonic = "ld4";
      form = form_4v;
      break;
    case NEON_ST1_1v:
      mnemonic = "st1";
      form = form_1v;
      break;
    case NEON_ST1_2v:
      mnemonic = "st1";
      form = form_2v;
      break;
    case NEON_ST1_3v:
      mnemonic = "st1";
      form = form_3v;
      break;
    case NEON_ST1_4v:
      mnemonic = "st1";
      form = form_4v;
      break;
    case NEON_ST2:
      mnemonic = "st2";
      form = form_2v;
      break;
    case NEON_ST3:
      mnemonic = "st3";
      form = form_3v;
      break;
    case NEON_ST4:
      mnemonic = "st4";
      form = form_4v;
      break;
    default:
      break;
  }

  // Work out unallocated encodings.
  bool allocated = (mnemonic != NULL);
  switch (instr->Mask(NEONLoadStoreMultiStructMask)) {
    case NEON_LD2:
    case NEON_LD3:
    case NEON_LD4:
    case NEON_ST2:
    case NEON_ST3:
    case NEON_ST4:
      // LD[2-4] and ST[2-4] cannot use .1d format.
      allocated = (instr->GetNEONQ() != 0) || (instr->GetNEONLSSize() != 3);
      break;
    default:
      break;
  }
  if (allocated) {
    VIXL_ASSERT(mnemonic != NULL);
    VIXL_ASSERT(form != NULL);
  } else {
    mnemonic = "unallocated";
    form = "(NEONLoadStoreMultiStruct)";
  }

  Format(instr, mnemonic, nfd.Substitute(form));
}


void Disassembler::VisitNEONLoadStoreMultiStructPostIndex(
    const Instruction *instr) {
  const char *mnemonic = NULL;
  const char *form = NULL;
  const char *form_1v = "{'Vt.%s}, ['Xns], 'Xmr1";
  const char *form_2v = "{'Vt.%s, 'Vt2.%s}, ['Xns], 'Xmr2";
  const char *form_3v = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s}, ['Xns], 'Xmr3";
  const char *form_4v = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s, 'Vt4.%s}, ['Xns], 'Xmr4";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LoadStoreFormatMap());

  switch (instr->Mask(NEONLoadStoreMultiStructPostIndexMask)) {
    case NEON_LD1_1v_post:
      mnemonic = "ld1";
      form = form_1v;
      break;
    case NEON_LD1_2v_post:
      mnemonic = "ld1";
      form = form_2v;
      break;
    case NEON_LD1_3v_post:
      mnemonic = "ld1";
      form = form_3v;
      break;
    case NEON_LD1_4v_post:
      mnemonic = "ld1";
      form = form_4v;
      break;
    case NEON_LD2_post:
      mnemonic = "ld2";
      form = form_2v;
      break;
    case NEON_LD3_post:
      mnemonic = "ld3";
      form = form_3v;
      break;
    case NEON_LD4_post:
      mnemonic = "ld4";
      form = form_4v;
      break;
    case NEON_ST1_1v_post:
      mnemonic = "st1";
      form = form_1v;
      break;
    case NEON_ST1_2v_post:
      mnemonic = "st1";
      form = form_2v;
      break;
    case NEON_ST1_3v_post:
      mnemonic = "st1";
      form = form_3v;
      break;
    case NEON_ST1_4v_post:
      mnemonic = "st1";
      form = form_4v;
      break;
    case NEON_ST2_post:
      mnemonic = "st2";
      form = form_2v;
      break;
    case NEON_ST3_post:
      mnemonic = "st3";
      form = form_3v;
      break;
    case NEON_ST4_post:
      mnemonic = "st4";
      form = form_4v;
      break;
    default:
      break;
  }

  // Work out unallocated encodings.
  bool allocated = (mnemonic != NULL);
  switch (instr->Mask(NEONLoadStoreMultiStructPostIndexMask)) {
    case NEON_LD2_post:
    case NEON_LD3_post:
    case NEON_LD4_post:
    case NEON_ST2_post:
    case NEON_ST3_post:
    case NEON_ST4_post:
      // LD[2-4] and ST[2-4] cannot use .1d format.
      allocated = (instr->GetNEONQ() != 0) || (instr->GetNEONLSSize() != 3);
      break;
    default:
      break;
  }
  if (allocated) {
    VIXL_ASSERT(mnemonic != NULL);
    VIXL_ASSERT(form != NULL);
  } else {
    mnemonic = "unallocated";
    form = "(NEONLoadStoreMultiStructPostIndex)";
  }

  Format(instr, mnemonic, nfd.Substitute(form));
}


void Disassembler::VisitNEONLoadStoreSingleStruct(const Instruction *instr) {
  const char *mnemonic = NULL;
  const char *form = NULL;

  const char *form_1b = "{'Vt.b}['IVLSLane0], ['Xns]";
  const char *form_1h = "{'Vt.h}['IVLSLane1], ['Xns]";
  const char *form_1s = "{'Vt.s}['IVLSLane2], ['Xns]";
  const char *form_1d = "{'Vt.d}['IVLSLane3], ['Xns]";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LoadStoreFormatMap());

  switch (instr->Mask(NEONLoadStoreSingleStructMask)) {
    case NEON_LD1_b:
      mnemonic = "ld1";
      form = form_1b;
      break;
    case NEON_LD1_h:
      mnemonic = "ld1";
      form = form_1h;
      break;
    case NEON_LD1_s:
      mnemonic = "ld1";
      VIXL_STATIC_ASSERT((NEON_LD1_s | (1 << NEONLSSize_offset)) == NEON_LD1_d);
      form = ((instr->GetNEONLSSize() & 1) == 0) ? form_1s : form_1d;
      break;
    case NEON_ST1_b:
      mnemonic = "st1";
      form = form_1b;
      break;
    case NEON_ST1_h:
      mnemonic = "st1";
      form = form_1h;
      break;
    case NEON_ST1_s:
      mnemonic = "st1";
      VIXL_STATIC_ASSERT((NEON_ST1_s | (1 << NEONLSSize_offset)) == NEON_ST1_d);
      form = ((instr->GetNEONLSSize() & 1) == 0) ? form_1s : form_1d;
      break;
    case NEON_LD1R:
      mnemonic = "ld1r";
      form = "{'Vt.%s}, ['Xns]";
      break;
    case NEON_LD2_b:
    case NEON_ST2_b:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld2" : "st2";
      form = "{'Vt.b, 'Vt2.b}['IVLSLane0], ['Xns]";
      break;
    case NEON_LD2_h:
    case NEON_ST2_h:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld2" : "st2";
      form = "{'Vt.h, 'Vt2.h}['IVLSLane1], ['Xns]";
      break;
    case NEON_LD2_s:
    case NEON_ST2_s:
      VIXL_STATIC_ASSERT((NEON_ST2_s | (1 << NEONLSSize_offset)) == NEON_ST2_d);
      VIXL_STATIC_ASSERT((NEON_LD2_s | (1 << NEONLSSize_offset)) == NEON_LD2_d);
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld2" : "st2";
      if ((instr->GetNEONLSSize() & 1) == 0) {
        form = "{'Vt.s, 'Vt2.s}['IVLSLane2], ['Xns]";
      } else {
        form = "{'Vt.d, 'Vt2.d}['IVLSLane3], ['Xns]";
      }
      break;
    case NEON_LD2R:
      mnemonic = "ld2r";
      form = "{'Vt.%s, 'Vt2.%s}, ['Xns]";
      break;
    case NEON_LD3_b:
    case NEON_ST3_b:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld3" : "st3";
      form = "{'Vt.b, 'Vt2.b, 'Vt3.b}['IVLSLane0], ['Xns]";
      break;
    case NEON_LD3_h:
    case NEON_ST3_h:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld3" : "st3";
      form = "{'Vt.h, 'Vt2.h, 'Vt3.h}['IVLSLane1], ['Xns]";
      break;
    case NEON_LD3_s:
    case NEON_ST3_s:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld3" : "st3";
      if ((instr->GetNEONLSSize() & 1) == 0) {
        form = "{'Vt.s, 'Vt2.s, 'Vt3.s}['IVLSLane2], ['Xns]";
      } else {
        form = "{'Vt.d, 'Vt2.d, 'Vt3.d}['IVLSLane3], ['Xns]";
      }
      break;
    case NEON_LD3R:
      mnemonic = "ld3r";
      form = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s}, ['Xns]";
      break;
    case NEON_LD4_b:
    case NEON_ST4_b:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld4" : "st4";
      form = "{'Vt.b, 'Vt2.b, 'Vt3.b, 'Vt4.b}['IVLSLane0], ['Xns]";
      break;
    case NEON_LD4_h:
    case NEON_ST4_h:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld4" : "st4";
      form = "{'Vt.h, 'Vt2.h, 'Vt3.h, 'Vt4.h}['IVLSLane1], ['Xns]";
      break;
    case NEON_LD4_s:
    case NEON_ST4_s:
      VIXL_STATIC_ASSERT((NEON_LD4_s | (1 << NEONLSSize_offset)) == NEON_LD4_d);
      VIXL_STATIC_ASSERT((NEON_ST4_s | (1 << NEONLSSize_offset)) == NEON_ST4_d);
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld4" : "st4";
      if ((instr->GetNEONLSSize() & 1) == 0) {
        form = "{'Vt.s, 'Vt2.s, 'Vt3.s, 'Vt4.s}['IVLSLane2], ['Xns]";
      } else {
        form = "{'Vt.d, 'Vt2.d, 'Vt3.d, 'Vt4.d}['IVLSLane3], ['Xns]";
      }
      break;
    case NEON_LD4R:
      mnemonic = "ld4r";
      form = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s, 'Vt4.%s}, ['Xns]";
      break;
    default:
      break;
  }

  // Work out unallocated encodings.
  bool allocated = (mnemonic != NULL);
  switch (instr->Mask(NEONLoadStoreSingleStructMask)) {
    case NEON_LD1_h:
    case NEON_LD2_h:
    case NEON_LD3_h:
    case NEON_LD4_h:
    case NEON_ST1_h:
    case NEON_ST2_h:
    case NEON_ST3_h:
    case NEON_ST4_h:
      VIXL_ASSERT(allocated);
      allocated = ((instr->GetNEONLSSize() & 1) == 0);
      break;
    case NEON_LD1_s:
    case NEON_LD2_s:
    case NEON_LD3_s:
    case NEON_LD4_s:
    case NEON_ST1_s:
    case NEON_ST2_s:
    case NEON_ST3_s:
    case NEON_ST4_s:
      VIXL_ASSERT(allocated);
      allocated = (instr->GetNEONLSSize() <= 1) &&
                  ((instr->GetNEONLSSize() == 0) || (instr->GetNEONS() == 0));
      break;
    case NEON_LD1R:
    case NEON_LD2R:
    case NEON_LD3R:
    case NEON_LD4R:
      VIXL_ASSERT(allocated);
      allocated = (instr->GetNEONS() == 0);
      break;
    default:
      break;
  }
  if (allocated) {
    VIXL_ASSERT(mnemonic != NULL);
    VIXL_ASSERT(form != NULL);
  } else {
    mnemonic = "unallocated";
    form = "(NEONLoadStoreSingleStruct)";
  }

  Format(instr, mnemonic, nfd.Substitute(form));
}


void Disassembler::VisitNEONLoadStoreSingleStructPostIndex(
    const Instruction *instr) {
  const char *mnemonic = NULL;
  const char *form = NULL;

  const char *form_1b = "{'Vt.b}['IVLSLane0], ['Xns], 'Xmb1";
  const char *form_1h = "{'Vt.h}['IVLSLane1], ['Xns], 'Xmb2";
  const char *form_1s = "{'Vt.s}['IVLSLane2], ['Xns], 'Xmb4";
  const char *form_1d = "{'Vt.d}['IVLSLane3], ['Xns], 'Xmb8";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LoadStoreFormatMap());

  switch (instr->Mask(NEONLoadStoreSingleStructPostIndexMask)) {
    case NEON_LD1_b_post:
      mnemonic = "ld1";
      form = form_1b;
      break;
    case NEON_LD1_h_post:
      mnemonic = "ld1";
      form = form_1h;
      break;
    case NEON_LD1_s_post:
      mnemonic = "ld1";
      VIXL_STATIC_ASSERT((NEON_LD1_s | (1 << NEONLSSize_offset)) == NEON_LD1_d);
      form = ((instr->GetNEONLSSize() & 1) == 0) ? form_1s : form_1d;
      break;
    case NEON_ST1_b_post:
      mnemonic = "st1";
      form = form_1b;
      break;
    case NEON_ST1_h_post:
      mnemonic = "st1";
      form = form_1h;
      break;
    case NEON_ST1_s_post:
      mnemonic = "st1";
      VIXL_STATIC_ASSERT((NEON_ST1_s | (1 << NEONLSSize_offset)) == NEON_ST1_d);
      form = ((instr->GetNEONLSSize() & 1) == 0) ? form_1s : form_1d;
      break;
    case NEON_LD1R_post:
      mnemonic = "ld1r";
      form = "{'Vt.%s}, ['Xns], 'Xmz1";
      break;
    case NEON_LD2_b_post:
    case NEON_ST2_b_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld2" : "st2";
      form = "{'Vt.b, 'Vt2.b}['IVLSLane0], ['Xns], 'Xmb2";
      break;
    case NEON_ST2_h_post:
    case NEON_LD2_h_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld2" : "st2";
      form = "{'Vt.h, 'Vt2.h}['IVLSLane1], ['Xns], 'Xmb4";
      break;
    case NEON_LD2_s_post:
    case NEON_ST2_s_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld2" : "st2";
      if ((instr->GetNEONLSSize() & 1) == 0)
        form = "{'Vt.s, 'Vt2.s}['IVLSLane2], ['Xns], 'Xmb8";
      else
        form = "{'Vt.d, 'Vt2.d}['IVLSLane3], ['Xns], 'Xmb16";
      break;
    case NEON_LD2R_post:
      mnemonic = "ld2r";
      form = "{'Vt.%s, 'Vt2.%s}, ['Xns], 'Xmz2";
      break;
    case NEON_LD3_b_post:
    case NEON_ST3_b_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld3" : "st3";
      form = "{'Vt.b, 'Vt2.b, 'Vt3.b}['IVLSLane0], ['Xns], 'Xmb3";
      break;
    case NEON_LD3_h_post:
    case NEON_ST3_h_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld3" : "st3";
      form = "{'Vt.h, 'Vt2.h, 'Vt3.h}['IVLSLane1], ['Xns], 'Xmb6";
      break;
    case NEON_LD3_s_post:
    case NEON_ST3_s_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld3" : "st3";
      if ((instr->GetNEONLSSize() & 1) == 0)
        form = "{'Vt.s, 'Vt2.s, 'Vt3.s}['IVLSLane2], ['Xns], 'Xmb12";
      else
        form = "{'Vt.d, 'Vt2.d, 'Vt3.d}['IVLSLane3], ['Xns], 'Xmb24";
      break;
    case NEON_LD3R_post:
      mnemonic = "ld3r";
      form = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s}, ['Xns], 'Xmz3";
      break;
    case NEON_LD4_b_post:
    case NEON_ST4_b_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld4" : "st4";
      form = "{'Vt.b, 'Vt2.b, 'Vt3.b, 'Vt4.b}['IVLSLane0], ['Xns], 'Xmb4";
      break;
    case NEON_LD4_h_post:
    case NEON_ST4_h_post:
      mnemonic = (instr->GetLdStXLoad()) == 1 ? "ld4" : "st4";
      form = "{'Vt.h, 'Vt2.h, 'Vt3.h, 'Vt4.h}['IVLSLane1], ['Xns], 'Xmb8";
      break;
    case NEON_LD4_s_post:
    case NEON_ST4_s_post:
      mnemonic = (instr->GetLdStXLoad() == 1) ? "ld4" : "st4";
      if ((instr->GetNEONLSSize() & 1) == 0)
        form = "{'Vt.s, 'Vt2.s, 'Vt3.s, 'Vt4.s}['IVLSLane2], ['Xns], 'Xmb16";
      else
        form = "{'Vt.d, 'Vt2.d, 'Vt3.d, 'Vt4.d}['IVLSLane3], ['Xns], 'Xmb32";
      break;
    case NEON_LD4R_post:
      mnemonic = "ld4r";
      form = "{'Vt.%s, 'Vt2.%s, 'Vt3.%s, 'Vt4.%s}, ['Xns], 'Xmz4";
      break;
    default:
      break;
  }

  // Work out unallocated encodings.
  bool allocated = (mnemonic != NULL);
  switch (instr->Mask(NEONLoadStoreSingleStructPostIndexMask)) {
    case NEON_LD1_h_post:
    case NEON_LD2_h_post:
    case NEON_LD3_h_post:
    case NEON_LD4_h_post:
    case NEON_ST1_h_post:
    case NEON_ST2_h_post:
    case NEON_ST3_h_post:
    case NEON_ST4_h_post:
      VIXL_ASSERT(allocated);
      allocated = ((instr->GetNEONLSSize() & 1) == 0);
      break;
    case NEON_LD1_s_post:
    case NEON_LD2_s_post:
    case NEON_LD3_s_post:
    case NEON_LD4_s_post:
    case NEON_ST1_s_post:
    case NEON_ST2_s_post:
    case NEON_ST3_s_post:
    case NEON_ST4_s_post:
      VIXL_ASSERT(allocated);
      allocated = (instr->GetNEONLSSize() <= 1) &&
                  ((instr->GetNEONLSSize() == 0) || (instr->GetNEONS() == 0));
      break;
    case NEON_LD1R_post:
    case NEON_LD2R_post:
    case NEON_LD3R_post:
    case NEON_LD4R_post:
      VIXL_ASSERT(allocated);
      allocated = (instr->GetNEONS() == 0);
      break;
    default:
      break;
  }
  if (allocated) {
    VIXL_ASSERT(mnemonic != NULL);
    VIXL_ASSERT(form != NULL);
  } else {
    mnemonic = "unallocated";
    form = "(NEONLoadStoreSingleStructPostIndex)";
  }

  Format(instr, mnemonic, nfd.Substitute(form));
}


void Disassembler::VisitNEONModifiedImmediate(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vt.%s, 'IVMIImm8, lsl 'IVMIShiftAmt1";

  switch (form_hash_) {
    case "movi_asimdimm_n_b"_h:
      form = "'Vt.'(3030?16:8)b, #0x'x1816:0905";
      break;
    case "bic_asimdimm_l_hl"_h:
    case "movi_asimdimm_l_hl"_h:
    case "mvni_asimdimm_l_hl"_h:
    case "orr_asimdimm_l_hl"_h:
      form = "'Vt.'?30:84h, #0x'x1816:0905'(1413?, lsl #'u1413*8)";
      break;
    case "movi_asimdimm_m_sm"_h:
    case "mvni_asimdimm_m_sm"_h:
      form = "'Vt.'?30:42s, #0x'x1816:0905, msl #'(1212?16:8)";
      break;
    case "bic_asimdimm_l_sl"_h:
    case "movi_asimdimm_l_sl"_h:
    case "mvni_asimdimm_l_sl"_h:
    case "orr_asimdimm_l_sl"_h:
      form = "'Vt.'?30:42s, #0x'x1816:0905'(1413?, lsl #'u1413*8)";
      break;
    case "movi_asimdimm_d_ds"_h:
      form = "'Dd, 'IVMIImm";
      break;
    case "movi_asimdimm_d2_d"_h:
      form = "'Vt.2d, 'IVMIImm";
      break;
    case "fmov_asimdimm_h_h"_h:
      form = "'Vt.'?30:84h, 'IFPNeon";
      break;
    case "fmov_asimdimm_s_s"_h:
      form = "'Vt.'?30:42s, 'IFPNeon";
      break;
    case "fmov_asimdimm_d2_d"_h:
      form = "'Vt.2d, 'IFPNeon";
      break;
  }

  Format(instr, mnemonic, form);
}

void Disassembler::DisassembleNEONScalar2RegMiscOnlyD(
    const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Dd, 'Dn";
  const char *suffix = ", #0";
  if (instr->GetNEONSize() != 3) {
    mnemonic = NULL;
  }
  switch (form_hash_) {
    case "abs_asisdmisc_r"_h:
    case "neg_asisdmisc_r"_h:
      suffix = NULL;
  }
  Format(instr, mnemonic, form, suffix);
}

void Disassembler::DisassembleNEONFPScalar2RegMisc(const Instruction *instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::FPScalarFormatMap());
  if (nfd.GetVectorFormat(0) == kFormatS) {  // Source format.
    VisitUnallocated(instr);
    return;
  }
  FormatWithDecodedMnemonic(instr, "'Sd, 'Dn");
}

void Disassembler::VisitNEONScalar2RegMisc(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, %sn";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::ScalarFormatMap());
  switch (form_hash_) {
    case "sqxtn_asisdmisc_n"_h:
    case "sqxtun_asisdmisc_n"_h:
    case "uqxtn_asisdmisc_n"_h:
      nfd.SetFormatMap(1, nfd.LongScalarFormatMap());
  }
  Format(instr, mnemonic, nfd.SubstitutePlaceholders(form));
}

void Disassembler::VisitNEONScalar3Diff(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, %sn, %sm";
  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::LongScalarFormatMap(),
                        NEONFormatDecoder::ScalarFormatMap());
  if (nfd.GetVectorFormat(0) == kFormatH) {
    mnemonic = NULL;
  }
  Format(instr, mnemonic, nfd.SubstitutePlaceholders(form));
}

void Disassembler::DisassembleNEONScalar3SameOnlyD(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Dd, 'Dn, 'Dm";
  if (instr->GetNEONSize() != 3) {
    mnemonic = NULL;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitNEONScalar3Same(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, %sn, %sm";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::ScalarFormatMap());
  VectorFormat vform = nfd.GetVectorFormat(0);
  switch (form_hash_) {
    case "srshl_asisdsame_only"_h:
    case "urshl_asisdsame_only"_h:
    case "sshl_asisdsame_only"_h:
    case "ushl_asisdsame_only"_h:
      if (vform != kFormatD) {
        mnemonic = NULL;
      }
      break;
    case "sqdmulh_asisdsame_only"_h:
    case "sqrdmulh_asisdsame_only"_h:
    case "sqrdmlah_asisdsame2_only"_h:
    case "sqrdmlsh_asisdsame2_only"_h:
      if ((vform == kFormatB) || (vform == kFormatD)) {
        mnemonic = NULL;
      }
  }
  Format(instr, mnemonic, nfd.SubstitutePlaceholders(form));
}

void Disassembler::DisassembleNEONScalarSatMulLongIndex(
    const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, %sn, 'Vf.%s['IVByElemIndex]";
  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::LongScalarFormatMap(),
                        NEONFormatDecoder::ScalarFormatMap());
  if (nfd.GetVectorFormat(0) == kFormatH) {
    mnemonic = NULL;
  }
  Format(instr,
         mnemonic,
         nfd.Substitute(form, nfd.kPlaceholder, nfd.kPlaceholder, nfd.kFormat));
}

void Disassembler::VisitNEONScalarByIndexedElement(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, %sn, 'Vf.%s['IVByElemIndex]";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::ScalarFormatMap());
  VectorFormat vform_dst = nfd.GetVectorFormat(0);
  if ((vform_dst == kFormatB) || (vform_dst == kFormatD)) {
    mnemonic = NULL;
  }
  Format(instr,
         mnemonic,
         nfd.Substitute(form, nfd.kPlaceholder, nfd.kPlaceholder, nfd.kFormat));
}


void Disassembler::VisitNEONScalarCopy(const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "(NEONScalarCopy)";

  NEONFormatDecoder nfd(instr, NEONFormatDecoder::TriangularScalarFormatMap());

  if (instr->Mask(NEONScalarCopyMask) == NEON_DUP_ELEMENT_scalar) {
    mnemonic = "mov";
    form = "%sd, 'Vn.%s['IVInsIndex1]";
  }

  Format(instr, mnemonic, nfd.Substitute(form, nfd.kPlaceholder, nfd.kFormat));
}


void Disassembler::VisitNEONScalarPairwise(const Instruction *instr) {
  VIXL_ASSERT(form_hash_ == "addp_asisdpair_only"_h);
  if (instr->GetNEONSize() == 3) {
    FormatWithDecodedMnemonic(instr, "'Dd, 'Vn.2d");
  } else {
    VisitUnallocated(instr);
  }
}

void Disassembler::DisassembleNEONScalarShiftImmOnlyD(
    const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Dd, 'Dn, ";
  const char *suffix = "'IsR";

  if (instr->ExtractBit(22) == 0) {
    // Only D registers are supported.
    mnemonic = NULL;
  }

  switch (form_hash_) {
    case "shl_asisdshf_r"_h:
    case "sli_asisdshf_r"_h:
      suffix = "'IsL";
  }

  Format(instr, mnemonic, form, suffix);
}

void Disassembler::DisassembleNEONScalarShiftRightNarrowImm(
    const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, %sn, 'IsR";
  static const NEONFormatMap map_dst =
      {{22, 21, 20, 19}, {NF_UNDEF, NF_B, NF_H, NF_H, NF_S, NF_S, NF_S, NF_S}};
  static const NEONFormatMap map_src =
      {{22, 21, 20, 19}, {NF_UNDEF, NF_H, NF_S, NF_S, NF_D, NF_D, NF_D, NF_D}};
  NEONFormatDecoder nfd(instr, &map_dst, &map_src);
  Format(instr, mnemonic, nfd.SubstitutePlaceholders(form));
}

void Disassembler::VisitNEONScalarShiftImmediate(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "%sd, %sn, ";
  const char *suffix = "'IsR";

  // clang-format off
  static const NEONFormatMap map = {{22, 21, 20, 19},
                                    {NF_UNDEF, NF_B, NF_H, NF_H,
                                     NF_S,     NF_S, NF_S, NF_S,
                                     NF_D,     NF_D, NF_D, NF_D,
                                     NF_D,     NF_D, NF_D, NF_D}};
  // clang-format on
  NEONFormatDecoder nfd(instr, &map);
  switch (form_hash_) {
    case "sqshlu_asisdshf_r"_h:
    case "sqshl_asisdshf_r"_h:
    case "uqshl_asisdshf_r"_h:
      suffix = "'IsL";
      break;
    default:
      if (nfd.GetVectorFormat(0) == kFormatB) {
        mnemonic = NULL;
      }
  }
  Format(instr, mnemonic, nfd.SubstitutePlaceholders(form), suffix);
}

void Disassembler::DisassembleNEONShiftLeftLongImm(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s";
  const char *suffix = ", 'IsL";

  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::ShiftLongNarrowImmFormatMap(),
                        NEONFormatDecoder::ShiftImmFormatMap());

  if (instr->GetImmNEONImmb() == 0 &&
      CountSetBits(instr->GetImmNEONImmh(), 32) == 1) {  // xtl variant.
    VIXL_ASSERT((form_hash_ == "sshll_asimdshf_l"_h) ||
                (form_hash_ == "ushll_asimdshf_l"_h));
    mnemonic = (form_hash_ == "sshll_asimdshf_l"_h) ? "sxtl" : "uxtl";
    suffix = NULL;
  }
  Format(instr, nfd.Mnemonic(mnemonic), nfd.Substitute(form), suffix);
}

void Disassembler::DisassembleNEONShiftRightImm(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'IsR";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::ShiftImmFormatMap());

  VectorFormat vform_dst = nfd.GetVectorFormat(0);
  if (vform_dst != kFormatUndefined) {
    uint32_t ls_dst = LaneSizeInBitsFromFormat(vform_dst);
    switch (form_hash_) {
      case "scvtf_asimdshf_c"_h:
      case "ucvtf_asimdshf_c"_h:
      case "fcvtzs_asimdshf_c"_h:
      case "fcvtzu_asimdshf_c"_h:
        if (ls_dst == kBRegSize) {
          mnemonic = NULL;
        }
        break;
    }
  }
  Format(instr, mnemonic, nfd.Substitute(form));
}

void Disassembler::DisassembleNEONShiftRightNarrowImm(
    const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'IsR";

  NEONFormatDecoder nfd(instr,
                        NEONFormatDecoder::ShiftImmFormatMap(),
                        NEONFormatDecoder::ShiftLongNarrowImmFormatMap());
  Format(instr, nfd.Mnemonic(mnemonic), nfd.Substitute(form));
}

void Disassembler::VisitNEONShiftImmediate(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Vd.%s, 'Vn.%s, 'IsL";
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::ShiftImmFormatMap());
  Format(instr, mnemonic, nfd.Substitute(form));
}


void Disassembler::VisitNEONTable(const Instruction *instr) {
  const char *form = "'Vd.'(3030?16:8)b, {'Vn.16b}, 'Vm.'(3030?16:8)b";
  const char *form_2v =
      "'Vd.'(3030?16:8)b, {'Vn.16b, 'Vn2.16b}, 'Vm.'(3030?16:8)b";
  const char *form_3v =
      "'Vd.'(3030?16:8)b, {'Vn.16b, 'Vn2.16b, 'Vn3.16b}, 'Vm.'(3030?16:8)b";
  const char *form_4v =
      "'Vd.'(3030?16:8)b, {'Vn.16b, 'Vn2.16b, 'Vn3.16b, 'Vn4.16b}, "
      "'Vm.'(3030?16:8)b";

  switch (form_hash_) {
    case "tbl_asimdtbl_l2_2"_h:
    case "tbx_asimdtbl_l2_2"_h:
      form = form_2v;
      break;
    case "tbl_asimdtbl_l3_3"_h:
    case "tbx_asimdtbl_l3_3"_h:
      form = form_3v;
      break;
    case "tbl_asimdtbl_l4_4"_h:
    case "tbx_asimdtbl_l4_4"_h:
      form = form_4v;
      break;
  }

  FormatWithDecodedMnemonic(instr, form);
}


void Disassembler::VisitNEONPerm(const Instruction *instr) {
  NEONFormatDecoder nfd(instr);
  FormatWithDecodedMnemonic(instr, nfd.Substitute("'Vd.%s, 'Vn.%s, 'Vm.%s"));
}

void Disassembler::VisitSVE32BitGatherLoad_VectorPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.s}, 'Pgl/z, ['Zn.s'(2016?, #'u2016)]";
  const char *form_h = "{'Zt.s}, 'Pgl/z, ['Zn.s'(2016?, #'u2016*2)]";
  const char *form_w = "{'Zt.s}, 'Pgl/z, ['Zn.s'(2016?, #'u2016*4)]";

  const char *mnemonic = mnemonic_.c_str();
  switch (form_hash_) {
    case "ld1h_z_p_ai_s"_h:
    case "ld1sh_z_p_ai_s"_h:
    case "ldff1h_z_p_ai_s"_h:
    case "ldff1sh_z_p_ai_s"_h:
      form = form_h;
      break;
    case "ld1w_z_p_ai_s"_h:
    case "ldff1w_z_p_ai_s"_h:
      form = form_w;
      break;
  }

  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVE32BitGatherPrefetch_VectorPlusImm(
    const Instruction *instr) {
  const char *form = "'prefSVEOp, 'Pgl, ['Zn.s'(2016?, #'u2016)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVE32BitScatterStore_VectorPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.s}, 'Pgl, ['Zn.s";
  const char *suffix = NULL;

  switch (form_hash_) {
    case "st1b_z_p_ai_s"_h:
      suffix = "'(2016?, #'u2016)]";
      break;
    case "st1h_z_p_ai_s"_h:
      suffix = "'(2016?, #'u2016*2)]";
      break;
    case "st1w_z_p_ai_s"_h:
      suffix = "'(2016?, #'u2016*4)]";
      break;
  }

  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVE64BitGatherLoad_VectorPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.d}, 'Pgl/z, ['Zn.d]";
  const char *form_imm[4] = {"{'Zt.d}, 'Pgl/z, ['Zn.d, #'u2016]",
                             "{'Zt.d}, 'Pgl/z, ['Zn.d, #'u2016*2]",
                             "{'Zt.d}, 'Pgl/z, ['Zn.d, #'u2016*4]",
                             "{'Zt.d}, 'Pgl/z, ['Zn.d, #'u2016*8]"};

  if (instr->ExtractBits(20, 16) != 0) {
    unsigned msz = instr->ExtractBits(24, 23);
    bool sign_extend = instr->ExtractBit(14) == 0;
    if ((msz == kDRegSizeInBytesLog2) && sign_extend) {
      form = "(SVE64BitGatherLoad_VectorPlusImm)";
    } else {
      VIXL_ASSERT(msz < ArrayLength(form_imm));
      form = form_imm[msz];
    }
  }

  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVE64BitGatherPrefetch_ScalarPlus64BitScaledOffsets(
    const Instruction *instr) {
  const char *form = "'prefSVEOp, 'Pgl, ['Xns, 'Zm.d'(1413?, lsl #'u1413)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::
    VisitSVE64BitGatherPrefetch_ScalarPlusUnpacked32BitScaledOffsets(
        const Instruction *instr) {
  const char *form =
      "'prefSVEOp, 'Pgl, ['Xns, 'Zm.d, '?22:suxtw'(2423? #'u2423)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVE64BitGatherPrefetch_VectorPlusImm(
    const Instruction *instr) {
  const char *form = "'prefSVEOp, 'Pgl, ['Zn.d'(2016?, #'u2016)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVE64BitScatterStore_VectorPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.d}, 'Pgl, ['Zn.d";
  const char *suffix = NULL;

  switch (form_hash_) {
    case "st1b_z_p_ai_d"_h:
      suffix = "'(2016?, #'u2016)]";
      break;
    case "st1h_z_p_ai_d"_h:
      suffix = "'(2016?, #'u2016*2)]";
      break;
    case "st1w_z_p_ai_d"_h:
      suffix = "'(2016?, #'u2016*4)]";
      break;
    case "st1d_z_p_ai_d"_h:
      suffix = "'(2016?, #'u2016*8)]";
      break;
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVEBitwiseLogicalWithImm_Unpredicated(
    const Instruction *instr) {
  if (instr->GetSVEImmLogical() == 0) {
    // The immediate encoded in the instruction is not in the expected format.
    Format(instr, "unallocated", "(SVEBitwiseImm)");
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'tl, 'Zd.'tl, 'ITriSvel");
  }
}

void Disassembler::VisitSVEBitwiseShiftByImm_Predicated(
    const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Zd.'tszp, 'Pgl/m, 'Zd.'tszp, ";
  const char *suffix = NULL;
  unsigned tsize = (instr->ExtractBits(23, 22) << 2) | instr->ExtractBits(9, 8);

  if (tsize == 0) {
    mnemonic = "unimplemented";
    form = "(SVEBitwiseShiftByImm_Predicated)";
  } else {
    switch (form_hash_) {
      case "lsl_z_p_zi"_h:
      case "sqshl_z_p_zi"_h:
      case "sqshlu_z_p_zi"_h:
      case "uqshl_z_p_zi"_h:
        suffix = "'ITriSvep";
        break;
      case "asrd_z_p_zi"_h:
      case "asr_z_p_zi"_h:
      case "lsr_z_p_zi"_h:
      case "srshr_z_p_zi"_h:
      case "urshr_z_p_zi"_h:
        suffix = "'ITriSveq";
        break;
      default:
        mnemonic = "unimplemented";
        form = "(SVEBitwiseShiftByImm_Predicated)";
        break;
    }
  }
  Format(instr, mnemonic, form, suffix);
}

void Disassembler::VisitSVEBitwiseShiftByWideElements_Predicated(
    const Instruction *instr) {
  if (instr->GetSVESize() == kDRegSizeInBytesLog2) {
    Format(instr, "unallocated", "(SVEBitwiseShiftByWideElements_Predicated)");
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl/m, 'Zd.'t, 'Zn.d");
  }
}

static bool SVEMoveMaskPreferred(uint64_t value, int lane_bytes_log2) {
  VIXL_ASSERT(IsUintN(8 << lane_bytes_log2, value));

  // Duplicate lane-sized value across double word.
  switch (lane_bytes_log2) {
    case 0:
      value *= 0x0101010101010101;
      break;
    case 1:
      value *= 0x0001000100010001;
      break;
    case 2:
      value *= 0x0000000100000001;
      break;
    case 3:  // Nothing to do
      break;
    default:
      VIXL_UNREACHABLE();
  }

  if ((value & 0xff) == 0) {
    // Check for 16-bit patterns. Set least-significant 16 bits, to make tests
    // easier; we already checked least-significant byte is zero above.
    uint64_t generic_value = value | 0xffff;

    // Check 0x00000000_0000pq00 or 0xffffffff_ffffpq00.
    if ((generic_value == 0xffff) || (generic_value == UINT64_MAX)) {
      return false;
    }

    // Check 0x0000pq00_0000pq00 or 0xffffpq00_ffffpq00.
    if (AllWordsMatch(value)) {
      generic_value &= 0xffffffff;
      if ((generic_value == 0xffff) || (generic_value == UINT32_MAX)) {
        return false;
      }
    }

    // Check 0xpq00pq00_pq00pq00.
    if (AllHalfwordsMatch(value)) {
      return false;
    }
  } else {
    // Check for 8-bit patterns. Set least-significant byte, to make tests
    // easier.
    uint64_t generic_value = value | 0xff;

    // Check 0x00000000_000000pq or 0xffffffff_ffffffpq.
    if ((generic_value == 0xff) || (generic_value == UINT64_MAX)) {
      return false;
    }

    // Check 0x000000pq_000000pq or 0xffffffpq_ffffffpq.
    if (AllWordsMatch(value)) {
      generic_value &= 0xffffffff;
      if ((generic_value == 0xff) || (generic_value == UINT32_MAX)) {
        return false;
      }
    }

    // Check 0x00pq00pq_00pq00pq or 0xffpqffpq_ffpqffpq.
    if (AllHalfwordsMatch(value)) {
      generic_value &= 0xffff;
      if ((generic_value == 0xff) || (generic_value == UINT16_MAX)) {
        return false;
      }
    }

    // Check 0xpqpqpqpq_pqpqpqpq.
    if (AllBytesMatch(value)) {
      return false;
    }
  }
  return true;
}

void Disassembler::VisitSVEBroadcastBitmaskImm(const Instruction *instr) {
  uint64_t imm = instr->GetSVEImmLogical();
  if (imm == 0) {
    VisitUnallocated(instr);
  } else {
    int lane_size = instr->GetSVEBitwiseImmLaneSizeInBytesLog2();
    const char *mnemonic =
        SVEMoveMaskPreferred(imm, lane_size) ? "mov" : "dupm";
    const char *form = "'Zd.'tl, 'ITriSvel";
    Format(instr, mnemonic, form);
  }
}

void Disassembler::VisitSVEBroadcastFPImm_Unpredicated(
    const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    // The preferred disassembly for fdup is "fmov".
    Format(instr, "fmov", "'Zd.'t, 'IFPSve");
  }
}

void Disassembler::VisitSVEBroadcastGeneralRegister(const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "(SVEBroadcastGeneralRegister)";

  switch (instr->Mask(SVEBroadcastGeneralRegisterMask)) {
    case DUP_z_r:
      // The preferred disassembly for dup is "mov".
      mnemonic = "mov";
      if (instr->GetSVESize() == kDRegSizeInBytesLog2) {
        form = "'Zd.'t, 'Xns";
      } else {
        form = "'Zd.'t, 'Wns";
      }
      break;
    default:
      break;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVEBroadcastIndexElement(const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "(SVEBroadcastIndexElement)";

  switch (instr->Mask(SVEBroadcastIndexElementMask)) {
    case DUP_z_zi: {
      // The tsz field must not be zero.
      int tsz = instr->ExtractBits(20, 16);
      if (tsz != 0) {
        // The preferred disassembly for dup is "mov".
        mnemonic = "mov";
        int imm2 = instr->ExtractBits(23, 22);
        if ((CountSetBits(imm2) + CountSetBits(tsz)) == 1) {
          // If imm2:tsz has one set bit, the index is zero. This is
          // disassembled as a mov from a b/h/s/d/q scalar register.
          form = "'Zd.'ti, 'ti'u0905";
        } else {
          form = "'Zd.'ti, 'Zn.'ti['IVInsSVEIndex]";
        }
      }
      break;
    }
    default:
      break;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVEBroadcastIntImm_Unpredicated(
    const Instruction *instr) {
  // The encoding of byte-sized lanes with lsl #8 is undefined.
  if ((instr->GetSVEVectorFormat() == kFormatVnB) &&
      (instr->ExtractBit(13) == 1)) {
    VisitUnallocated(instr);
  } else {
    // The preferred disassembly for dup is "mov".
    Format(instr, "mov", "'Zd.'t, #'s1205'(1313?, lsl #8)");
  }
}

void Disassembler::VisitSVECompressActiveElements(const Instruction *instr) {
  // The top bit of size is always set for compact, so 't can only be
  // substituted with types S and D.
  if (instr->ExtractBit(23) == 1) {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl, 'Zn.'t");
  } else {
    VisitUnallocated(instr);
  }
}

void Disassembler::VisitSVEConditionallyExtractElementToGeneralRegister(
    const Instruction *instr) {
  const char *form = "'Wd, 'Pgl, 'Wd, 'Zn.'t";

  if (instr->GetSVESize() == kDRegSizeInBytesLog2) {
    form = "'Xd, p'u1210, 'Xd, 'Zn.'t";
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEConditionallyTerminateScalars(
    const Instruction *instr) {
  FormatWithDecodedMnemonic(instr, "'R22n, 'R22m");
}

void Disassembler::VisitSVEContiguousFirstFaultLoad_ScalarPlusScalar(
    const Instruction *instr) {
  const char *form = "{'Zt.'tlss}, 'Pgl/z, ['Xns";
  const char *suffix = "'(2016=31?:, 'Xm)]";

  switch (form_hash_) {
    case "ldff1h_z_p_br_u16"_h:
    case "ldff1h_z_p_br_u32"_h:
    case "ldff1h_z_p_br_u64"_h:
    case "ldff1sh_z_p_br_s32"_h:
    case "ldff1sh_z_p_br_s64"_h:
      suffix = "'(2016=31?:, 'Xm, lsl #1)]";
      break;
    case "ldff1w_z_p_br_u32"_h:
    case "ldff1w_z_p_br_u64"_h:
    case "ldff1sw_z_p_br_s64"_h:
      suffix = "'(2016=31?:, 'Xm, lsl #2)]";
      break;
    case "ldff1d_z_p_br_u64"_h:
      suffix = "'(2016=31?:, 'Xm, lsl #3)]";
      break;
  }

  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVEContiguousNonFaultLoad_ScalarPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.'tlss}, 'Pgl/z, ['Xns'(1916?, #'s1916, mul vl)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEContiguousNonTemporalLoad_ScalarPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.b}, 'Pgl/z, ['Xns";
  const char *suffix = "'(1916?, #'s1916, mul vl)]";

  switch (form_hash_) {
    case "ldnt1d_z_p_bi_contiguous"_h:
      form = "{'Zt.d}, 'Pgl/z, ['Xns";
      break;
    case "ldnt1h_z_p_bi_contiguous"_h:
      form = "{'Zt.h}, 'Pgl/z, ['Xns";
      break;
    case "ldnt1w_z_p_bi_contiguous"_h:
      form = "{'Zt.s}, 'Pgl/z, ['Xns";
      break;
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVEContiguousNonTemporalStore_ScalarPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.b}, 'Pgl, ['Xns";
  const char *suffix = "'(1916?, #'s1916, mul vl)]";

  switch (form_hash_) {
    case "stnt1d_z_p_bi_contiguous"_h:
      form = "{'Zt.d}, 'Pgl, ['Xns";
      break;
    case "stnt1h_z_p_bi_contiguous"_h:
      form = "{'Zt.h}, 'Pgl, ['Xns";
      break;
    case "stnt1w_z_p_bi_contiguous"_h:
      form = "{'Zt.s}, 'Pgl, ['Xns";
      break;
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVEContiguousPrefetch_ScalarPlusImm(
    const Instruction *instr) {
  const char *form = "'prefSVEOp, 'Pgl, ['Xns'(2116?, #'s2116, mul vl)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEContiguousPrefetch_ScalarPlusScalar(
    const Instruction *instr) {
  if (instr->GetRm() == kZeroRegCode) {
    VisitUnallocated(instr);
    return;
  }

  const char *form = "'prefSVEOp, 'Pgl, ['Xns, 'Rm'(2423?, lsl #'u2423)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEContiguousStore_ScalarPlusImm(
    const Instruction *instr) {
  // The 'size' field isn't in the usual place here.
  const char *form = "{'Zt.'tls}, 'Pgl, ['Xns'(1916?, #'s1916, mul vl)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVECopyFPImm_Predicated(const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "(SVECopyFPImm_Predicated)";

  if (instr->GetSVEVectorFormat() != kFormatVnB) {
    switch (instr->Mask(SVECopyFPImm_PredicatedMask)) {
      case FCPY_z_p_i:
        // The preferred disassembly for fcpy is "fmov".
        mnemonic = "fmov";
        form = "'Zd.'t, 'Pm/m, 'IFPSve";
        break;
      default:
        break;
    }
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVECopyGeneralRegisterToVector_Predicated(
    const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "(SVECopyGeneralRegisterToVector_Predicated)";

  switch (instr->Mask(SVECopyGeneralRegisterToVector_PredicatedMask)) {
    case CPY_z_p_r:
      // The preferred disassembly for cpy is "mov".
      mnemonic = "mov";
      form = "'Zd.'t, 'Pgl/m, 'Wns";
      if (instr->GetSVESize() == kXRegSizeInBytesLog2) {
        form = "'Zd.'t, 'Pgl/m, 'Xns";
      }
      break;
    default:
      break;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVECopyIntImm_Predicated(const Instruction *instr) {
  // The preferred disassembly for cpy is "mov".
  const char *form = "'Zd.'t, 'Pm/'?14:mz, #'s1205'(1313?, lsl #8)";
  Format(instr, "mov", form);
}

void Disassembler::VisitSVECopySIMDFPScalarRegisterToVector_Predicated(
    const Instruction *instr) {
  Format(instr, "mov", "'Zd.'t, 'Pgl/m, 'Vnv");
}

void Disassembler::VisitSVEExtractElementToGeneralRegister(
    const Instruction *instr) {
  const char *form = "'Wd, 'Pgl, 'Zn.'t";
  if (instr->GetSVESize() == kDRegSizeInBytesLog2) {
    form = "'Xd, p'u1210, 'Zn.'t";
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEFPArithmeticWithImm_Predicated(
    const Instruction *instr) {
  const char *form = "'Zd.'t, 'Pgl/m, 'Zd.'t, #";
  const char *suffix00 = "0.0";
  const char *suffix05 = "0.5";
  const char *suffix10 = "1.0";
  const char *suffix20 = "2.0";
  int i1 = instr->ExtractBit(5);
  const char *suffix = i1 ? suffix10 : suffix00;

  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
    return;
  }

  switch (form_hash_) {
    case "fadd_z_p_zs"_h:
    case "fsubr_z_p_zs"_h:
    case "fsub_z_p_zs"_h:
      suffix = i1 ? suffix10 : suffix05;
      break;
    case "fmul_z_p_zs"_h:
      suffix = i1 ? suffix20 : suffix05;
      break;
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVEFPArithmetic_Predicated(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl/m, 'Zd.'t, 'Zn.'t");
  }
}

void Disassembler::VisitSVEFPConvertPrecision(const Instruction *instr) {
  const char *form = NULL;

  switch (form_hash_) {
    case "fcvt_z_p_z_d2h"_h:
      form = "'Zd.h, 'Pgl/m, 'Zn.d";
      break;
    case "fcvt_z_p_z_d2s"_h:
      form = "'Zd.s, 'Pgl/m, 'Zn.d";
      break;
    case "fcvt_z_p_z_h2d"_h:
      form = "'Zd.d, 'Pgl/m, 'Zn.h";
      break;
    case "fcvt_z_p_z_h2s"_h:
      form = "'Zd.s, 'Pgl/m, 'Zn.h";
      break;
    case "fcvt_z_p_z_s2d"_h:
      form = "'Zd.d, 'Pgl/m, 'Zn.s";
      break;
    case "fcvt_z_p_z_s2h"_h:
      form = "'Zd.h, 'Pgl/m, 'Zn.s";
      break;
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEFPExponentialAccelerator(const Instruction *instr) {
  unsigned size = instr->GetSVESize();
  if ((size == kHRegSizeInBytesLog2) || (size == kSRegSizeInBytesLog2) ||
      (size == kDRegSizeInBytesLog2)) {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'t");
  } else {
    VisitUnallocated(instr);
  }
}

void Disassembler::VisitSVEFPRoundToIntegralValue(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl/m, 'Zn.'t");
  }
}

void Disassembler::VisitSVEFPTrigMulAddCoefficient(const Instruction *instr) {
  unsigned size = instr->GetSVESize();
  if ((size == kHRegSizeInBytesLog2) || (size == kSRegSizeInBytesLog2) ||
      (size == kDRegSizeInBytesLog2)) {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zd.'t, 'Zn.'t, #'u1816");
  } else {
    VisitUnallocated(instr);
  }
}

void Disassembler::VisitSVEFPTrigSelectCoefficient(const Instruction *instr) {
  unsigned size = instr->GetSVESize();
  if ((size == kHRegSizeInBytesLog2) || (size == kSRegSizeInBytesLog2) ||
      (size == kDRegSizeInBytesLog2)) {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'t, 'Zm.'t");
  } else {
    VisitUnallocated(instr);
  }
}

void Disassembler::VisitSVEFPUnaryOp(const Instruction *instr) {
  if (instr->GetSVESize() == kBRegSizeInBytesLog2) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl/m, 'Zn.'t");
  }
}

static const char *IncDecFormHelper(const Instruction *instr,
                                    const char *reg_pat_mul_form,
                                    const char *reg_pat_form,
                                    const char *reg_form) {
  if (instr->ExtractBits(19, 16) == 0) {
    if (instr->ExtractBits(9, 5) == SVE_ALL) {
      // Use the register only form if the multiplier is one (encoded as zero)
      // and the pattern is SVE_ALL.
      return reg_form;
    }
    // Use the register and pattern form if the multiplier is one.
    return reg_pat_form;
  }
  return reg_pat_mul_form;
}

void Disassembler::VisitSVEIncDecRegisterByElementCount(
    const Instruction *instr) {
  const char *form =
      IncDecFormHelper(instr, "'Xd, 'Ipc, mul #'u1916+1", "'Xd, 'Ipc", "'Xd");
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEIncDecVectorByElementCount(
    const Instruction *instr) {
  const char *form = IncDecFormHelper(instr,
                                      "'Zd.'t, 'Ipc, mul #'u1916+1",
                                      "'Zd.'t, 'Ipc",
                                      "'Zd.'t");
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEInsertGeneralRegister(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Wn";
  if (instr->GetSVESize() == kDRegSizeInBytesLog2) {
    form = "'Zd.'t, 'Xn";
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEIntAddSubtractImm_Unpredicated(
    const Instruction *instr) {
  const char *form = "'Zd.'t, 'Zd.'t, #'u1205'(1313?, lsl #8)";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEIntCompareScalarCountAndLimit(
    const Instruction *instr) {
  FormatWithDecodedMnemonic(instr, "'Pd.'t, 'R12n, 'R12m");
}

void Disassembler::VisitSVEIntConvertToFP(const Instruction *instr) {
  const char *form = NULL;
  switch (form_hash_) {
    case "scvtf_z_p_z_h2fp16"_h:
    case "ucvtf_z_p_z_h2fp16"_h:
      form = "'Zd.h, 'Pgl/m, 'Zn.h";
      break;
    case "scvtf_z_p_z_w2d"_h:
    case "ucvtf_z_p_z_w2d"_h:
      form = "'Zd.d, 'Pgl/m, 'Zn.s";
      break;
    case "scvtf_z_p_z_w2fp16"_h:
    case "ucvtf_z_p_z_w2fp16"_h:
      form = "'Zd.h, 'Pgl/m, 'Zn.s";
      break;
    case "scvtf_z_p_z_w2s"_h:
    case "ucvtf_z_p_z_w2s"_h:
      form = "'Zd.s, 'Pgl/m, 'Zn.s";
      break;
    case "scvtf_z_p_z_x2d"_h:
    case "ucvtf_z_p_z_x2d"_h:
      form = "'Zd.d, 'Pgl/m, 'Zn.d";
      break;
    case "scvtf_z_p_z_x2fp16"_h:
    case "ucvtf_z_p_z_x2fp16"_h:
      form = "'Zd.h, 'Pgl/m, 'Zn.d";
      break;
    case "scvtf_z_p_z_x2s"_h:
    case "ucvtf_z_p_z_x2s"_h:
      form = "'Zd.s, 'Pgl/m, 'Zn.d";
      break;
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEIntDivideVectors_Predicated(
    const Instruction *instr) {
  unsigned size = instr->GetSVESize();
  if ((size == kSRegSizeInBytesLog2) || (size == kDRegSizeInBytesLog2)) {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl/m, 'Zd.'t, 'Zn.'t");
  } else {
    VisitUnallocated(instr);
  }
}

void Disassembler::VisitSVELoadAndBroadcastElement(const Instruction *instr) {
  const char *form = "{'Zt.d}, 'Pgl/z, ['Xns";
  const char *suffix = "'(2116?, #'u2116)]";
  const char *suffix_h = "'(2116?, #'u2116*2)]";
  const char *suffix_w = "'(2116?, #'u2116*4)]";
  const char *suffix_d = "'(2116?, #'u2116*8)]";

  switch (form_hash_) {
    case "ld1rb_z_p_bi_u8"_h:
      form = "{'Zt.b}, 'Pgl/z, ['Xns";
      break;
    case "ld1rb_z_p_bi_u16"_h:
    case "ld1rsb_z_p_bi_s16"_h:
      form = "{'Zt.h}, 'Pgl/z, ['Xns";
      break;
    case "ld1rb_z_p_bi_u32"_h:
    case "ld1rsb_z_p_bi_s32"_h:
      form = "{'Zt.s}, 'Pgl/z, ['Xns";
      break;
    case "ld1rh_z_p_bi_u16"_h:
      form = "{'Zt.h}, 'Pgl/z, ['Xns";
      suffix = suffix_h;
      break;
    case "ld1rh_z_p_bi_u32"_h:
    case "ld1rsh_z_p_bi_s32"_h:
      form = "{'Zt.s}, 'Pgl/z, ['Xns";
      suffix = suffix_h;
      break;
    case "ld1rh_z_p_bi_u64"_h:
    case "ld1rsh_z_p_bi_s64"_h:
      suffix = suffix_h;
      break;
    case "ld1rw_z_p_bi_u32"_h:
      form = "{'Zt.s}, 'Pgl/z, ['Xns";
      suffix = suffix_w;
      break;
    case "ld1rsw_z_p_bi_s64"_h:
    case "ld1rw_z_p_bi_u64"_h:
      suffix = suffix_w;
      break;
    case "ld1rd_z_p_bi_u64"_h:
      suffix = suffix_d;
      break;
  }

  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVELoadAndBroadcastQOWord_ScalarPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.'tmsz}, 'Pgl/z, ['Xns";
  const char *suffix = "'(1916?, #'s1916*16)]";

  switch (form_hash_) {
    case "ld1rob_z_p_bi_u8"_h:
    case "ld1rod_z_p_bi_u64"_h:
    case "ld1roh_z_p_bi_u16"_h:
    case "ld1row_z_p_bi_u32"_h:
      suffix = "'(1916?, #'s1916*32)]";
      break;
  }

  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVELoadPredicateRegister(const Instruction *instr) {
  const char *form = "'Pd, ['Xns, #'s2116:1210, mul vl]";
  if (instr->Mask(0x003f1c00) == 0) {
    form = "'Pd, ['Xns]";
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVELoadVectorRegister(const Instruction *instr) {
  const char *form = "'Zt, ['Xns, #'s2116:1210, mul vl]";
  if (instr->Mask(0x003f1c00) == 0) {
    form = "'Zd, ['Xns]";
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEReversePredicateElements(const Instruction *instr) {
  FormatWithDecodedMnemonic(instr, "'Pd.'t, 'Pn.'t");
}

void Disassembler::VisitSVEReverseVectorElements(const Instruction *instr) {
  FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'t");
}

void Disassembler::VisitSVEReverseWithinElements(const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "'Zd.'t, 'Pgl/m, 'Zn.'t";

  unsigned size = instr->GetSVESize();
  switch (instr->Mask(SVEReverseWithinElementsMask)) {
    case RBIT_z_p_z:
      mnemonic = "rbit";
      break;
    case REVB_z_z:
      if ((size == kHRegSizeInBytesLog2) || (size == kSRegSizeInBytesLog2) ||
          (size == kDRegSizeInBytesLog2)) {
        mnemonic = "revb";
      } else {
        form = "(SVEReverseWithinElements)";
      }
      break;
    case REVH_z_z:
      if ((size == kSRegSizeInBytesLog2) || (size == kDRegSizeInBytesLog2)) {
        mnemonic = "revh";
      } else {
        form = "(SVEReverseWithinElements)";
      }
      break;
    case REVW_z_z:
      if (size == kDRegSizeInBytesLog2) {
        mnemonic = "revw";
      } else {
        form = "(SVEReverseWithinElements)";
      }
      break;
    default:
      break;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVESaturatingIncDecRegisterByElementCount(
    const Instruction *instr) {
  const char *form = IncDecFormHelper(instr,
                                      "'R20d, 'Ipc, mul #'u1916+1",
                                      "'R20d, 'Ipc",
                                      "'R20d");
  const char *form_sx = IncDecFormHelper(instr,
                                         "'Xd, 'Wd, 'Ipc, mul #'u1916+1",
                                         "'Xd, 'Wd, 'Ipc",
                                         "'Xd, 'Wd");

  switch (form_hash_) {
    case "sqdecb_r_rs_sx"_h:
    case "sqdecd_r_rs_sx"_h:
    case "sqdech_r_rs_sx"_h:
    case "sqdecw_r_rs_sx"_h:
    case "sqincb_r_rs_sx"_h:
    case "sqincd_r_rs_sx"_h:
    case "sqinch_r_rs_sx"_h:
    case "sqincw_r_rs_sx"_h:
      form = form_sx;
      break;
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVESaturatingIncDecVectorByElementCount(
    const Instruction *instr) {
  const char *form = IncDecFormHelper(instr,
                                      "'Zd.'t, 'Ipc, mul #'u1916+1",
                                      "'Zd.'t, 'Ipc",
                                      "'Zd.'t");
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEStorePredicateRegister(const Instruction *instr) {
  const char *form = "'Pd, ['Xns, #'s2116:1210, mul vl]";
  if (instr->Mask(0x003f1c00) == 0) {
    form = "'Pd, ['Xns]";
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEStoreVectorRegister(const Instruction *instr) {
  const char *form = "'Zt, ['Xns, #'s2116:1210, mul vl]";
  if (instr->Mask(0x003f1c00) == 0) {
    form = "'Zd, ['Xns]";
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEUnpackVectorElements(const Instruction *instr) {
  if (instr->GetSVESize() == 0) {
    // The lowest lane size of the destination vector is H-sized lane.
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'th");
  }
}

void Disassembler::VisitSVEAddressGeneration(const Instruction *instr) {
  const char *form = "'Zd.d, ['Zn.d, 'Zm.d";
  const char *suffix = NULL;

  switch (form_hash_) {
    case "adr_z_az_d_s32_scaled"_h:
      suffix = ", sxtw'(1110? #'u1110)]";
      break;
    case "adr_z_az_d_u32_scaled"_h:
      suffix = ", uxtw'(1110? #'u1110)]";
      break;
    case "adr_z_az_sd_same_scaled"_h:
      form = "'Zd.'t, ['Zn.'t, 'Zm.'t";
      suffix = "'(1110?, lsl #'u1110)]";
      break;
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVEBitwiseLogicalUnpredicated(
    const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "'Zd.d, 'Zn.d, 'Zm.d";

  switch (instr->Mask(SVEBitwiseLogicalUnpredicatedMask)) {
    case AND_z_zz:
      mnemonic = "and";
      break;
    case BIC_z_zz:
      mnemonic = "bic";
      break;
    case EOR_z_zz:
      mnemonic = "eor";
      break;
    case ORR_z_zz:
      mnemonic = "orr";
      if (instr->GetRn() == instr->GetRm()) {
        mnemonic = "mov";
        form = "'Zd.d, 'Zn.d";
      }
      break;
    default:
      break;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVEBitwiseShiftUnpredicated(const Instruction *instr) {
  const char *mnemonic = "unimplemented";
  const char *form = "(SVEBitwiseShiftUnpredicated)";
  unsigned tsize =
      (instr->ExtractBits(23, 22) << 2) | instr->ExtractBits(20, 19);
  unsigned lane_size = instr->GetSVESize();

  const char *suffix = NULL;
  const char *form_i = "'Zd.'tszs, 'Zn.'tszs, ";

  switch (form_hash_) {
    case "asr_z_zi"_h:
    case "lsr_z_zi"_h:
    case "sri_z_zzi"_h:
    case "srsra_z_zi"_h:
    case "ssra_z_zi"_h:
    case "ursra_z_zi"_h:
    case "usra_z_zi"_h:
      if (tsize != 0) {
        // The tsz field must not be zero.
        mnemonic = mnemonic_.c_str();
        form = form_i;
        suffix = "'ITriSves";
      }
      break;
    case "lsl_z_zi"_h:
    case "sli_z_zzi"_h:
      if (tsize != 0) {
        // The tsz field must not be zero.
        mnemonic = mnemonic_.c_str();
        form = form_i;
        suffix = "'ITriSver";
      }
      break;
    case "asr_z_zw"_h:
    case "lsl_z_zw"_h:
    case "lsr_z_zw"_h:
      if (lane_size <= kSRegSizeInBytesLog2) {
        mnemonic = mnemonic_.c_str();
        form = "'Zd.'t, 'Zn.'t, 'Zm.d";
      }
      break;
    default:
      break;
  }

  Format(instr, mnemonic, form, suffix);
}

void Disassembler::VisitSVEElementCount(const Instruction *instr) {
  const char *form =
      IncDecFormHelper(instr, "'Xd, 'Ipc, mul #'u1916+1", "'Xd, 'Ipc", "'Xd");
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEFPAccumulatingReduction(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'t'u0400, 'Pgl, 't'u0400, 'Zn.'t");
  }
}

void Disassembler::VisitSVEFPArithmeticUnpredicated(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'t, 'Zm.'t");
  }
}

void Disassembler::VisitSVEFPCompareVectors(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Pd.'t, 'Pgl/z, 'Zn.'t, 'Zm.'t");
  }
}

void Disassembler::VisitSVEFPCompareWithZero(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Pd.'t, 'Pgl/z, 'Zn.'t, #0.0");
  }
}

void Disassembler::VisitSVEFPComplexAddition(const Instruction *instr) {
  // Bit 15 is always set, so this gives 90 * 1 or 3.
  const char *form = "'Zd.'t, 'Pgl/m, 'Zd.'t, 'Zn.'t, #'u1615*90";
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, form);
  }
}

void Disassembler::VisitSVEFPComplexMulAdd(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Pgl/m, 'Zn.'t, 'Zm.'t, #'u1413*90";
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, form);
  }
}

void Disassembler::VisitSVEFPFastReduction(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'t'u0400, 'Pgl, 'Zn.'t");
  }
}

void Disassembler::VisitSVEFPMulAdd(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl/m, 'Zn.'t, 'Zm.'t");
  }
}

void Disassembler::VisitSVEFPUnaryOpUnpredicated(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    VisitUnallocated(instr);
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'t");
  }
}

void Disassembler::VisitSVEIndexGeneration(const Instruction *instr) {
  const char *form = "'Zd.'t, ";
  const char *suffix = "#'s0905, #'s2016";

  switch (form_hash_) {
    case "index_z_ir"_h:
      suffix = "#'s0905, '(2322=3?'Xm:'Wm)";
      break;
    case "index_z_ri"_h:
      suffix = "'(2322=3?'Xn:'Wn), #'s2016";
      break;
    case "index_z_rr"_h:
      suffix = "'(2322=3?'Xn:'Wn), '(2322=3?'Xm:'Wm)";
      break;
  }
  FormatWithDecodedMnemonic(instr, form, suffix);
}

void Disassembler::VisitSVEIntMulAddUnpredicated(const Instruction *instr) {
  if (static_cast<unsigned>(instr->GetSVESize()) >= kSRegSizeInBytesLog2) {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'tq, 'Zm.'tq");
  } else {
    VisitUnallocated(instr);
  }
}

void Disassembler::VisitSVEIntReduction(const Instruction *instr) {
  const char *form = "'Vdv, 'Pgl, 'Zn.'t";
  switch (form_hash_) {
    case "saddv_r_p_z"_h:
    case "uaddv_r_p_z"_h:
      form = "'Dd, 'Pgl, 'Zn.'t";
      break;
  }
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEIntUnaryArithmeticPredicated(
    const Instruction *instr) {
  VectorFormat vform = instr->GetSVEVectorFormat();

  switch (form_hash_) {
    case "sxtw_z_p_z"_h:
    case "uxtw_z_p_z"_h:
      if (vform == kFormatVnS) {
        VisitUnallocated(instr);
        return;
      }
      VIXL_FALLTHROUGH();
    case "sxth_z_p_z"_h:
    case "uxth_z_p_z"_h:
      if (vform == kFormatVnH) {
        VisitUnallocated(instr);
        return;
      }
      VIXL_FALLTHROUGH();
    case "sxtb_z_p_z"_h:
    case "uxtb_z_p_z"_h:
    case "fabs_z_p_z"_h:
    case "fneg_z_p_z"_h:
      if (vform == kFormatVnB) {
        VisitUnallocated(instr);
        return;
      }
      break;
  }

  FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Pgl/m, 'Zn.'t");
}

void Disassembler::VisitSVEMulIndex(const Instruction *instr) {
  const char *form = "'Zd.s, 'Zn.b, z'u1816.b['u2019]";

  switch (form_hash_) {
    case "sdot_z_zzzi_d"_h:
    case "udot_z_zzzi_d"_h:
      form = "'Zd.d, 'Zn.h, z'u1916.h['u2020]";
      break;
  }

  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEPredicateLogical(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Pd.b, p'u1310/z, 'Pn.b, 'Pm.b";

  int pd = instr->GetPd();
  int pn = instr->GetPn();
  int pm = instr->GetPm();
  int pg = instr->ExtractBits(13, 10);

  switch (form_hash_) {
    case "ands_p_p_pp_z"_h:
      if (pn == pm) {
        mnemonic = "movs";
        form = "'Pd.b, p'u1310/z, 'Pn.b";
      }
      break;
    case "and_p_p_pp_z"_h:
      if (pn == pm) {
        mnemonic = "mov";
        form = "'Pd.b, p'u1310/z, 'Pn.b";
      }
      break;
    case "eors_p_p_pp_z"_h:
      if (pm == pg) {
        mnemonic = "nots";
        form = "'Pd.b, 'Pm/z, 'Pn.b";
      }
      break;
    case "eor_p_p_pp_z"_h:
      if (pm == pg) {
        mnemonic = "not";
        form = "'Pd.b, 'Pm/z, 'Pn.b";
      }
      break;
    case "orrs_p_p_pp_z"_h:
      if ((pn == pm) && (pn == pg)) {
        mnemonic = "movs";
        form = "'Pd.b, 'Pn.b";
      }
      break;
    case "orr_p_p_pp_z"_h:
      if ((pn == pm) && (pn == pg)) {
        mnemonic = "mov";
        form = "'Pd.b, 'Pn.b";
      }
      break;
    case "sel_p_p_pp"_h:
      if (pd == pm) {
        mnemonic = "mov";
        form = "'Pd.b, p'u1310/m, 'Pn.b";
      } else {
        form = "'Pd.b, p'u1310, 'Pn.b, 'Pm.b";
      }
      break;
  }
  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVEPredicateInitialize(const Instruction *instr) {
  const char *form = "'Pd.'t'(0905=31?:, 'Ipc)";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitSVEVectorSelect(const Instruction *instr) {
  const char *mnemonic = mnemonic_.c_str();
  const char *form = "'Zd.'t, p'u1310, 'Zn.'t, 'Zm.'t";

  if (instr->GetRd() == instr->GetRm()) {
    mnemonic = "mov";
    form = "'Zd.'t, p'u1310/m, 'Zn.'t";
  }

  Format(instr, mnemonic, form);
}

void Disassembler::VisitSVEContiguousLoad_ScalarPlusImm(
    const Instruction *instr) {
  const char *form = "{'Zt.'tlss}, 'Pgl/z, ['Xns'(1916?, #'s1916, mul vl)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::VisitReserved(const Instruction *instr) {
  // UDF is the only instruction in this group, and the Decoder is precise.
  VIXL_ASSERT(instr->Mask(ReservedMask) == UDF);
  Format(instr, "udf", "'IUdf");
}

void Disassembler::VisitUnimplemented(const Instruction *instr) {
  Format(instr, "unimplemented", "(Unimplemented)");
}


void Disassembler::VisitUnallocated(const Instruction *instr) {
  Format(instr, "unallocated", "(Unallocated)");
}

void Disassembler::Visit(Metadata *metadata, const Instruction *instr) {
  VIXL_ASSERT(metadata->count("form") > 0);
  const std::string &form = (*metadata)["form"];
  form_hash_ = Hash(form.c_str());
  FormToStringMap::const_iterator its = form_to_string_.find(form_hash_);
  if (its != form_to_string_.end()) {
    SetMnemonicFromForm(form);
    FormatWithDecodedMnemonic(instr, its->second);
    return;
  }

  const FormToVisitorFnMap *fv = Disassembler::GetFormToVisitorFnMap();
  FormToVisitorFnMap::const_iterator it = fv->find(form_hash_);
  if (it == fv->end()) {
    VisitUnimplemented(instr);
  } else {
    SetMnemonicFromForm(form);
    (it->second)(this, instr);
  }
}

void Disassembler::Disassemble_PdT_PgZ_ZnT_ZmT(const Instruction *instr) {
  const char *form = "'Pd.'t, 'Pgl/z, 'Zn.'t, 'Zm.'t";
  VectorFormat vform = instr->GetSVEVectorFormat();

  if ((vform == kFormatVnS) || (vform == kFormatVnD)) {
    Format(instr, "unimplemented", "(PdT_PgZ_ZnT_ZmT)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::Disassemble_ZdB_ZnB_ZmB(const Instruction *instr) {
  const char *form = "'Zd.b, 'Zn.b, 'Zm.b";
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    Format(instr, mnemonic_.c_str(), form);
  } else {
    Format(instr, "unimplemented", "(ZdB_ZnB_ZmB)");
  }
}

void Disassembler::Disassemble_ZdS_PgM_ZnS(const Instruction *instr) {
  const char *form = "'Zd.s, 'Pgl/m, 'Zn.s";
  if (instr->GetSVEVectorFormat() == kFormatVnS) {
    Format(instr, mnemonic_.c_str(), form);
  } else {
    Format(instr, "unimplemented", "(ZdS_PgM_ZnS)");
  }
}

void Disassembler::DisassembleSVEFlogb(const Instruction *instr) {
  const char *form = "'Zd.'tf, 'Pgl/m, 'Zn.'tf";
  if (instr->GetSVEVectorFormat(17) == kFormatVnB) {
    Format(instr, "unimplemented", "(SVEFlogb)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::Disassemble_ZdT_PgZ_ZnT_ZmT(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Pgl/z, 'Zn.'t, 'Zm.'t";
  VectorFormat vform = instr->GetSVEVectorFormat();
  if ((vform == kFormatVnS) || (vform == kFormatVnD)) {
    Format(instr, mnemonic_.c_str(), form);
  } else {
    Format(instr, "unimplemented", "(ZdT_PgZ_ZnT_ZmT)");
  }
}

void Disassembler::Disassemble_ZdT_ZnT_ZmTb(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Zn.'t, 'Zm.'th";
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    Format(instr, "unimplemented", "(ZdT_ZnT_ZmTb)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::Disassemble_ZdT_ZnTb(const Instruction *instr) {
  const char *form = "'Zd.'tszs, 'Zn.'tszd";
  std::pair<int, int> shift_and_lane_size =
      instr->GetSVEImmShiftAndLaneSizeLog2(/* is_predicated = */ false);
  int shift_dist = shift_and_lane_size.first;
  int lane_size = shift_and_lane_size.second;
  // Convert shift_dist from a right to left shift. Valid xtn instructions
  // must have a left shift_dist equivalent of zero.
  shift_dist = (8 << lane_size) - shift_dist;
  if ((lane_size >= static_cast<int>(kBRegSizeInBytesLog2)) &&
      (lane_size <= static_cast<int>(kSRegSizeInBytesLog2)) &&
      (shift_dist == 0)) {
    Format(instr, mnemonic_.c_str(), form);
  } else {
    Format(instr, "unimplemented", "(ZdT_ZnTb)");
  }
}

void Disassembler::DisassembleSVEPmull(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnS) {
    VisitUnallocated(instr);
  } else {
    Disassemble_ZdT_ZnTb_ZmTb(instr);
  }
}

void Disassembler::Disassemble_ZdT_ZnTb_ZmTb(const Instruction *instr) {
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    // TODO: This is correct for saddlbt, ssublbt, subltb, which don't have
    // b-lane sized form, but may need changes for other instructions reaching
    // here.
    Format(instr, "unimplemented", "(ZdT_ZnTb_ZmTb)");
  } else {
    FormatWithDecodedMnemonic(instr, "'Zd.'t, 'Zn.'th, 'Zm.'th");
  }
}

void Disassembler::DisassembleSVEAddSubHigh(const Instruction *instr) {
  const char *form = "'Zd.'th, 'Zn.'t, 'Zm.'t";
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    Format(instr, "unimplemented", "(SVEAddSubHigh)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::DisassembleSVEShiftLeftImm(const Instruction *instr) {
  const char *form = "'Zd.'tszd, 'Zn.'tszs, 'ITriSver";
  std::pair<int, int> shift_and_lane_size =
      instr->GetSVEImmShiftAndLaneSizeLog2(/* is_predicated = */ false);
  int lane_size = shift_and_lane_size.second;
  if ((lane_size >= static_cast<int>(kBRegSizeInBytesLog2)) &&
      (lane_size <= static_cast<int>(kSRegSizeInBytesLog2))) {
    Format(instr, mnemonic_.c_str(), form);
  } else {
    Format(instr, "unimplemented", "(SVEShiftLeftImm)");
  }
}

void Disassembler::DisassembleSVEShiftRightImm(const Instruction *instr) {
  const char *form = "'Zd.'tszs, 'Zn.'tszd, 'ITriSves";
  std::pair<int, int> shift_and_lane_size =
      instr->GetSVEImmShiftAndLaneSizeLog2(/* is_predicated = */ false);
  int lane_size = shift_and_lane_size.second;
  if ((lane_size >= static_cast<int>(kBRegSizeInBytesLog2)) &&
      (lane_size <= static_cast<int>(kSRegSizeInBytesLog2))) {
    Format(instr, mnemonic_.c_str(), form);
  } else {
    Format(instr, "unimplemented", "(SVEShiftRightImm)");
  }
}

void Disassembler::Disassemble_ZdaT_PgM_ZnTb(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Pgl/m, 'Zn.'th";

  if (instr->GetSVESize() == 0) {
    // The lowest lane size of the destination vector is H-sized lane.
    Format(instr, "unimplemented", "(Disassemble_ZdaT_PgM_ZnTb)");
    return;
  }

  Format(instr, mnemonic_.c_str(), form);
}

void Disassembler::Disassemble_ZdaT_ZnTb_ZmTb(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Zn.'th, 'Zm.'th";
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    Format(instr, "unimplemented", "(ZdaT_ZnTb_ZmTb)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::Disassemble_ZdaT_ZnTb_ZmTb_const(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Zn.'tq, 'Zm.'tq, #'u1110*90";
  VectorFormat vform = instr->GetSVEVectorFormat();

  if ((vform == kFormatVnB) || (vform == kFormatVnH)) {
    Format(instr, "unimplemented", "(ZdaT_ZnTb_ZmTb_const)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::DisassembleSVEFPPair(const Instruction *instr) {
  const char *form = "'Zd.'t, 'Pgl/m, 'Zd.'t, 'Zn.'t";
  if (instr->GetSVEVectorFormat() == kFormatVnB) {
    Format(instr, "unimplemented", "(SVEFPPair)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::DisassembleSVEComplexIntAddition(const Instruction *instr) {
  // Bit 10: 0 => #90, 1 => #270.
  const char *form = "'Zd.'t, 'Zd.'t, 'Zn.'t, #'(1010?270:90)";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::Disassemble_ZdnT_ZdnT_ZmT_const(const Instruction *instr) {
  const char *form = "'Zd.'tszs, 'Zd.'tszs, 'Zn.'tszs, 'ITriSves";
  unsigned tsize =
      (instr->ExtractBits(23, 22) << 2) | instr->ExtractBits(20, 19);

  if (tsize == 0) {
    Format(instr, "unimplemented", "(ZdnT_ZdnT_ZmT_const)");
  } else {
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::Disassemble_ZtD_PgZ_ZnD_Xm(const Instruction *instr) {
  const char *form = "{'Zt.d}, 'Pgl/z, ['Zn.d'(2016=31?:, 'Xm)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::Disassemble_ZtD_Pg_ZnD_Xm(const Instruction *instr) {
  const char *form = "{'Zt.d}, 'Pgl, ['Zn.d'(2016=31?:, 'Xm)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::Disassemble_ZtS_PgZ_ZnS_Xm(const Instruction *instr) {
  const char *form = "{'Zt.s}, 'Pgl/z, ['Zn.s'(2016=31?:, 'Xm)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::Disassemble_ZtS_Pg_ZnS_Xm(const Instruction *instr) {
  const char *form = "{'Zt.s}, 'Pgl, ['Zn.s'(2016=31?:, 'Xm)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::Disassemble_XdSP_XnSP_Xm(const Instruction *instr) {
  const char *form = "'Xds, 'Xns'(2016=31?:, 'Xm)";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::Disassemble_XdSP_XnSP_uimm6_uimm4(const Instruction *instr) {
  VIXL_STATIC_ASSERT(kMTETagGranuleInBytes == 16);
  const char *form = "'Xds, 'Xns, #'u2116*16, #'u1310";
  Format(instr, mnemonic_.c_str(), form);
}

void Disassembler::Disassemble_Xd_XnSP_XmSP(const Instruction *instr) {
  if ((form_hash_ == Hash("subps_64s_dp_2src")) && (instr->GetRd() == 31)) {
    Format(instr, "cmpp", "'Xns, 'Xms");
  } else {
    const char *form = "'Xd, 'Xns, 'Xms";
    Format(instr, mnemonic_.c_str(), form);
  }
}

void Disassembler::DisassembleMTEStoreTagPair(const Instruction *instr) {
  const char *form = "'Xt, 'Xt2, ['Xns";
  const char *suffix = NULL;
  switch (form_hash_) {
    case Hash("stgp_64_ldstpair_off"):
      suffix = "'(2115?, #'s2115*16)]";
      break;
    case Hash("stgp_64_ldstpair_post"):
      suffix = "], #'s2115*16";
      break;
    case Hash("stgp_64_ldstpair_pre"):
      suffix = ", #'s2115*16]!";
      break;
    default:
      mnemonic_ = "unimplemented";
      break;
  }

  Format(instr, mnemonic_.c_str(), form, suffix);
}

void Disassembler::DisassembleMTEStoreTag(const Instruction *instr) {
  const char *form = "'Xds, ['Xns";
  const char *suffix = NULL;
  switch (form_hash_) {
    case Hash("st2g_64soffset_ldsttags"):
    case Hash("stg_64soffset_ldsttags"):
    case Hash("stz2g_64soffset_ldsttags"):
    case Hash("stzg_64soffset_ldsttags"):
      suffix = "'(2012?, #'s2012*16)]";
      break;
    case Hash("st2g_64spost_ldsttags"):
    case Hash("stg_64spost_ldsttags"):
    case Hash("stz2g_64spost_ldsttags"):
    case Hash("stzg_64spost_ldsttags"):
      suffix = "], #'s2012*16";
      break;
    case Hash("st2g_64spre_ldsttags"):
    case Hash("stg_64spre_ldsttags"):
    case Hash("stz2g_64spre_ldsttags"):
    case Hash("stzg_64spre_ldsttags"):
      suffix = ", #'s2012*16]!";
      break;
    default:
      mnemonic_ = "unimplemented";
      break;
  }

  Format(instr, mnemonic_.c_str(), form, suffix);
}

void Disassembler::DisassembleMTELoadTag(const Instruction *instr) {
  const char *form = "'Xt, ['Xns'(2012?, #'s2012*16)]";
  FormatWithDecodedMnemonic(instr, form);
}

void Disassembler::DisassembleCpy(const Instruction *instr) {
  const char *form = "['Xd]!, ['Xs]!, 'Xn!";

  int d = instr->GetRd();
  int n = instr->GetRn();
  int s = instr->GetRs();

  // Aliased registers and sp/zr are disallowed.
  if ((d == n) || (d == s) || (n == s) || (d == 31) || (n == 31) || (s == 31)) {
    form = NULL;
  }

  // Bits 31 and 30 must be zero.
  if (instr->ExtractBits(31, 30)) {
    form = NULL;
  }

  Format(instr, mnemonic_.c_str(), form);
}

void Disassembler::DisassembleSet(const Instruction *instr) {
  const char *form = "['Xd]!, 'Xn!, 'Xs";

  int d = instr->GetRd();
  int n = instr->GetRn();
  int s = instr->GetRs();

  // Aliased registers are disallowed. Only Xs may be xzr.
  if ((d == n) || (d == s) || (n == s) || (d == 31) || (n == 31)) {
    form = NULL;
  }

  // Bits 31 and 30 must be zero.
  if (instr->ExtractBits(31, 30)) {
    form = NULL;
  }

  Format(instr, mnemonic_.c_str(), form);
}

void Disassembler::ProcessOutput(const Instruction * /*instr*/) {
  // The base disasm does nothing more than disassembling into a buffer.
}


void Disassembler::AppendRegisterNameToOutput(const Instruction *instr,
                                              const CPURegister &reg) {
  USE(instr);
  VIXL_ASSERT(reg.IsValid());
  char reg_char;

  if (reg.IsRegister()) {
    reg_char = reg.Is64Bits() ? 'x' : 'w';
  } else {
    VIXL_ASSERT(reg.IsVRegister());
    switch (reg.GetSizeInBits()) {
      case kBRegSize:
        reg_char = 'b';
        break;
      case kHRegSize:
        reg_char = 'h';
        break;
      case kSRegSize:
        reg_char = 's';
        break;
      case kDRegSize:
        reg_char = 'd';
        break;
      default:
        VIXL_ASSERT(reg.Is128Bits());
        reg_char = 'q';
    }
  }

  if (reg.IsVRegister() || !(reg.Aliases(sp) || reg.Aliases(xzr))) {
    // A core or scalar/vector register: [wx]0 - 30, [bhsdq]0 - 31.
    AppendToOutput("%c%d", reg_char, reg.GetCode());
  } else if (reg.Aliases(sp)) {
    // Disassemble w31/x31 as stack pointer wsp/sp.
    AppendToOutput("%s", reg.Is64Bits() ? "sp" : "wsp");
  } else {
    // Disassemble w31/x31 as zero register wzr/xzr.
    AppendToOutput("%czr", reg_char);
  }
}


void Disassembler::AppendPCRelativeOffsetToOutput(const Instruction *instr,
                                                  int64_t offset) {
  USE(instr);
  if (offset < 0) {
    // Cast to uint64_t so that INT64_MIN is handled in a well-defined way.
    uint64_t abs_offset = UnsignedNegate(static_cast<uint64_t>(offset));
    AppendToOutput("#-0x%" PRIx64, abs_offset);
  } else {
    AppendToOutput("#+0x%" PRIx64, offset);
  }
}


void Disassembler::AppendAddressToOutput(const Instruction *instr,
                                         const void *addr) {
  USE(instr);
  AppendToOutput("(addr 0x%" PRIxPTR ")", reinterpret_cast<uintptr_t>(addr));
}


void Disassembler::AppendCodeAddressToOutput(const Instruction *instr,
                                             const void *addr) {
  AppendAddressToOutput(instr, addr);
}


void Disassembler::AppendDataAddressToOutput(const Instruction *instr,
                                             const void *addr) {
  AppendAddressToOutput(instr, addr);
}


void Disassembler::AppendCodeRelativeAddressToOutput(const Instruction *instr,
                                                     const void *addr) {
  USE(instr);
  int64_t rel_addr = CodeRelativeAddress(addr);
  if (rel_addr >= 0) {
    AppendToOutput("(addr 0x%" PRIx64 ")", rel_addr);
  } else {
    AppendToOutput("(addr -0x%" PRIx64 ")", -rel_addr);
  }
}


void Disassembler::AppendCodeRelativeCodeAddressToOutput(
    const Instruction *instr, const void *addr) {
  AppendCodeRelativeAddressToOutput(instr, addr);
}


void Disassembler::AppendCodeRelativeDataAddressToOutput(
    const Instruction *instr, const void *addr) {
  AppendCodeRelativeAddressToOutput(instr, addr);
}


void Disassembler::MapCodeAddress(int64_t base_address,
                                  const Instruction *instr_address) {
  set_code_address_offset(base_address -
                          reinterpret_cast<intptr_t>(instr_address));
}
int64_t Disassembler::CodeRelativeAddress(const void *addr) {
  return reinterpret_cast<intptr_t>(addr) + code_address_offset();
}


void Disassembler::Format(const Instruction *instr,
                          const char *mnemonic,
                          const char *format0,
                          const char *format1) {
  if ((mnemonic == NULL) || (format0 == NULL)) {
    VisitUnallocated(instr);
  } else {
    ResetOutput();
    Substitute(instr, mnemonic);
    if (format0[0] != 0) {  // Not a zero-length string.
      VIXL_ASSERT(buffer_pos_ < buffer_size_);
      buffer_[buffer_pos_++] = ' ';
      int chars_written = Substitute(instr, format0);
      // TODO: consider using a zero-length string here, too.
      if (format1 != NULL) {
        chars_written += Substitute(instr, format1);
      }

      if (chars_written == 0) {
        // Erase the space written earlier, as there are no arguments to the
        // instruction.
        buffer_pos_--;
      }
    }
    VIXL_ASSERT(buffer_pos_ < buffer_size_);
    buffer_[buffer_pos_] = 0;
    ProcessOutput(instr);
  }
}

void Disassembler::FormatWithDecodedMnemonic(const Instruction *instr,
                                             const char *format0,
                                             const char *format1) {
  Format(instr, mnemonic_.c_str(), format0, format1);
}

int Disassembler::Substitute(const Instruction *instr, const char *string) {
  uint32_t buffer_pos_init = buffer_pos_;
  char chr = *string++;
  while (chr != '\0') {
    if (chr == '\'') {
      string += SubstituteField(instr, string);
    } else {
      VIXL_ASSERT(buffer_pos_ < buffer_size_);
      buffer_[buffer_pos_++] = chr;
    }
    chr = *string++;
  }
  return static_cast<int>(buffer_pos_ - buffer_pos_init);
}


int Disassembler::SubstituteField(const Instruction *instr,
                                  const char *format) {
  switch (format[0]) {
    // NB. The remaining substitution prefix upper-case characters are: JU.
    case 'R':  // Register. X or W, selected by sf (or alternative) bit.
    case 'F':  // FP register. S or D, selected by type field.
    case 'V':  // Vector register, V, vector format.
    case 'Z':  // Scalable vector register.
    case 'W':
    case 'X':
    case 'B':
    case 'H':
    case 'S':
    case 'D':
    case 'Q':
      return SubstituteRegisterField(instr, format);
    case 'P':
      return SubstitutePredicateRegisterField(instr, format);
    case 'I':
      return SubstituteImmediateField(instr, format);
    case 'L':
      return SubstituteLiteralField(instr, format);
    case 'N':
      return SubstituteShiftField(instr, format);
    case 'C':
      return SubstituteConditionField(instr, format);
    case 'E':
      return SubstituteExtendField(instr, format);
    case 'A':
      return SubstitutePCRelAddressField(instr, format);
    case 'T':
      return SubstituteBranchTargetField(instr, format);
    case 'O':
      return SubstituteLSRegOffsetField(instr, format);
    case 'M':
      return SubstituteBarrierField(instr, format);
    case 'K':
      return SubstituteCrField(instr, format);
    case 'G':
      return SubstituteSysOpField(instr, format);
    case 'p':
      return SubstitutePrefetchField(instr, format);
    case 'u':
    case 's':
    case 'x':
      return SubstituteIntField(instr, format);
    case 't':
      return SubstituteSVESize(instr, format);
    case '?':
      return SubstituteTernary(instr, format);
    case '(':
      return SubstituteConditionalBlock(instr, format);
    default: {
      VIXL_UNREACHABLE();
      return 1;
    }
  }
}

std::pair<unsigned, unsigned> Disassembler::GetRegNumForField(
    const Instruction *instr, char reg_prefix, const char *field) {
  unsigned reg_num = UINT_MAX;
  unsigned field_len = 1;

  switch (field[0]) {
    case 'd':
      reg_num = instr->GetRd();
      break;
    case 'n':
      reg_num = instr->GetRn();
      break;
    case 'm':
      reg_num = instr->GetRm();
      break;
    case 'e':
      // This is register Rm, but using a 4-bit specifier. Used in NEON
      // by-element instructions.
      reg_num = instr->GetRmLow16();
      break;
    case 'f':
      // This is register Rm, but using an element size dependent number of bits
      // in the register specifier.
      reg_num =
          (instr->GetNEONSize() < 2) ? instr->GetRmLow16() : instr->GetRm();
      break;
    case 'a':
      reg_num = instr->GetRa();
      break;
    case 's':
      reg_num = instr->GetRs();
      break;
    case 't':
      reg_num = instr->GetRt();
      break;
    default:
      VIXL_UNREACHABLE();
  }

  switch (field[1]) {
    case '2':
    case '3':
    case '4':
      if ((reg_prefix == 'V') || (reg_prefix == 'Z')) {  // t2/3/4, n2/3/4
        VIXL_ASSERT((field[0] == 't') || (field[0] == 'n'));
        reg_num = (reg_num + field[1] - '1') % 32;
        field_len++;
      } else {
        VIXL_ASSERT((field[0] == 't') && (field[1] == '2'));
        reg_num = instr->GetRt2();
        field_len++;
      }
      break;
    case '+':  // Rt+, Rs+ (ie. Rt + 1, Rs + 1)
      VIXL_ASSERT((reg_prefix == 'W') || (reg_prefix == 'X'));
      VIXL_ASSERT((field[0] == 's') || (field[0] == 't'));
      reg_num++;
      field_len++;
      break;
    case 's':  // Core registers that are (w)sp rather than zr.
      VIXL_ASSERT((reg_prefix == 'W') || (reg_prefix == 'X'));
      reg_num = (reg_num == kZeroRegCode) ? kSPRegInternalCode : reg_num;
      field_len++;
      break;
  }

  VIXL_ASSERT(reg_num != UINT_MAX);
  return std::make_pair(reg_num, field_len);
}

int Disassembler::SubstituteRegisterField(const Instruction *instr,
                                          const char *format) {
  unsigned field_len = 1;  // Initially, count only the first character.

  // The first character of the register format field, eg R, X, S, etc.
  char reg_prefix = format[0];

  // Pointer to the character after the prefix. This may be one of the standard
  // symbols representing a register encoding, or a two digit bit position,
  // handled by the following code.
  const char *reg_field = &format[1];

  if (reg_prefix == 'R') {
    bool is_x = instr->GetSixtyFourBits() == 1;
    if (strspn(reg_field, "0123456789") == 2) {  // r20d, r31n, etc.
      // Core W or X registers where the type is determined by a specified bit
      // position, eg. 'R20d, 'R05n. This is like the 'Rd syntax, where bit 31
      // is implicitly used to select between W and X.
      int bitpos = ((reg_field[0] - '0') * 10) + (reg_field[1] - '0');
      VIXL_ASSERT(bitpos <= 31);
      is_x = (instr->ExtractBit(bitpos) == 1);
      reg_field = &format[3];
      field_len += 2;
    }
    reg_prefix = is_x ? 'X' : 'W';
  }

  std::pair<unsigned, unsigned> rn =
      GetRegNumForField(instr, reg_prefix, reg_field);
  unsigned reg_num = rn.first;
  field_len += rn.second;

  if (reg_field[0] == 'm') {
    switch (reg_field[1]) {
      // Handle registers tagged with b (bytes), z (instruction), or
      // r (registers), used for address updates in NEON load/store
      // instructions.
      case 'r':
      case 'b':
      case 'z': {
        VIXL_ASSERT(reg_prefix == 'X');
        field_len = 3;
        char *eimm;
        int imm = static_cast<int>(strtol(&reg_field[2], &eimm, 10));
        field_len += static_cast<unsigned>(eimm - &reg_field[2]);
        if (reg_num == 31) {
          switch (reg_field[1]) {
            case 'z':
              imm *= (1 << instr->GetNEONLSSize());
              break;
            case 'r':
              imm *= (instr->GetNEONQ() == 0) ? kDRegSizeInBytes
                                              : kQRegSizeInBytes;
              break;
            case 'b':
              break;
          }
          AppendToOutput("#%d", imm);
          return field_len;
        }
        break;
      }
    }
  }

  CPURegister::RegisterType reg_type = CPURegister::kRegister;
  unsigned reg_size = kXRegSize;

  if (reg_prefix == 'F') {
    switch (instr->GetFPType()) {
      case 3:
        reg_prefix = 'H';
        break;
      case 0:
        reg_prefix = 'S';
        break;
      default:
        reg_prefix = 'D';
    }
  }

  switch (reg_prefix) {
    case 'W':
      reg_type = CPURegister::kRegister;
      reg_size = kWRegSize;
      break;
    case 'X':
      reg_type = CPURegister::kRegister;
      reg_size = kXRegSize;
      break;
    case 'B':
      reg_type = CPURegister::kVRegister;
      reg_size = kBRegSize;
      break;
    case 'H':
      reg_type = CPURegister::kVRegister;
      reg_size = kHRegSize;
      break;
    case 'S':
      reg_type = CPURegister::kVRegister;
      reg_size = kSRegSize;
      break;
    case 'D':
      reg_type = CPURegister::kVRegister;
      reg_size = kDRegSize;
      break;
    case 'Q':
      reg_type = CPURegister::kVRegister;
      reg_size = kQRegSize;
      break;
    case 'V':
      if (reg_field[1] == 'v') {
        reg_type = CPURegister::kVRegister;
        reg_size = 1 << (instr->GetSVESize() + 3);
        field_len++;
        break;
      }
      AppendToOutput("v%d", reg_num);
      return field_len;
    case 'Z':
      AppendToOutput("z%d", reg_num);
      return field_len;
    default:
      VIXL_UNREACHABLE();
  }

  AppendRegisterNameToOutput(instr, CPURegister(reg_num, reg_size, reg_type));

  return field_len;
}

int Disassembler::SubstitutePredicateRegisterField(const Instruction *instr,
                                                   const char *format) {
  VIXL_ASSERT(format[0] == 'P');
  switch (format[1]) {
    // This field only supports P register that are always encoded in the same
    // position.
    case 'd':
    case 't':
      AppendToOutput("p%u", instr->GetPt());
      break;
    case 'n':
      AppendToOutput("p%u", instr->GetPn());
      break;
    case 'm':
      AppendToOutput("p%u", instr->GetPm());
      break;
    case 'g':
      VIXL_ASSERT(format[2] == 'l');
      AppendToOutput("p%u", instr->GetPgLow8());
      return 3;
    default:
      VIXL_UNREACHABLE();
  }
  return 2;
}

int Disassembler::SubstituteImmediateField(const Instruction *instr,
                                           const char *format) {
  VIXL_ASSERT(format[0] == 'I');

  switch (format[1]) {
    case 'M': {  // IMoveImm, IMoveNeg or IMoveLSL.
      if (format[5] == 'L') {
        AppendToOutput("#0x%" PRIx32, instr->GetImmMoveWide());
        if (instr->GetShiftMoveWide() > 0) {
          AppendToOutput(", lsl #%" PRId32, 16 * instr->GetShiftMoveWide());
        }
      } else {
        VIXL_ASSERT((format[5] == 'I') || (format[5] == 'N'));
        uint64_t imm = static_cast<uint64_t>(instr->GetImmMoveWide())
                       << (16 * instr->GetShiftMoveWide());
        if (format[5] == 'N') imm = ~imm;
        if (!instr->GetSixtyFourBits()) imm &= UINT64_C(0xffffffff);
        AppendToOutput("#0x%" PRIx64, imm);
      }
      return 8;
    }
    case 'L': {
      switch (format[2]) {
        case 'L': {  // ILLiteral - Immediate Load Literal.
          AppendToOutput("pc%+" PRId32,
                         instr->GetImmLLiteral() *
                             static_cast<int>(kLiteralEntrySize));
          return 9;
        }
        case 'S': {  // ILS - Immediate Load/Store.
                     // ILSi - As above, but an index field which must not be
                     // omitted even if it is zero.
          bool is_index = format[3] == 'i';
          if (is_index || (instr->GetImmLS() != 0)) {
            AppendToOutput(", #%" PRId32, instr->GetImmLS());
          }
          return is_index ? 4 : 3;
        }
        case 'P': {  // ILPx - Immediate Load/Store Pair, x = access size.
                     // ILPxi - As above, but an index field which must not be
                     // omitted even if it is zero.
          VIXL_ASSERT((format[3] >= '0') && (format[3] <= '9'));
          bool is_index = format[4] == 'i';
          if (is_index || (instr->GetImmLSPair() != 0)) {
            // format[3] is the scale value. Convert to a number.
            int scale = 1 << (format[3] - '0');
            AppendToOutput(", #%" PRId32, instr->GetImmLSPair() * scale);
          }
          return is_index ? 5 : 4;
        }
        case 'U': {  // ILU - Immediate Load/Store Unsigned.
          if (instr->GetImmLSUnsigned() != 0) {
            int shift = instr->GetSizeLS();
            AppendToOutput(", #%" PRId32, instr->GetImmLSUnsigned() << shift);
          }
          return 3;
        }
        case 'A': {  // ILA - Immediate Load with pointer authentication.
          if (instr->GetImmLSPAC() != 0) {
            AppendToOutput(", #%" PRId32, instr->GetImmLSPAC());
          }
          return 3;
        }
        default: {
          VIXL_UNIMPLEMENTED();
          return 0;
        }
      }
    }
    case 'C': {  // ICondB - Immediate Conditional Branch.
      int64_t offset = instr->GetImmCondBranch() << 2;
      AppendPCRelativeOffsetToOutput(instr, offset);
      return 6;
    }
    case 'A': {  // IAddSub.
      int64_t imm = instr->GetImmAddSub() << (12 * instr->GetImmAddSubShift());
      AppendToOutput("#0x%" PRIx64 " (%" PRId64 ")", imm, imm);
      return 7;
    }
    case 'F': {  // IFP, IFPNeon, IFPSve or IFPFBits.
      int imm8 = 0;
      size_t len = strlen("IFP");
      switch (format[3]) {
        case 'F':
          VIXL_ASSERT(strncmp(format, "IFPFBits", strlen("IFPFBits")) == 0);
          AppendToOutput("#%" PRId32, 64 - instr->GetFPScale());
          return static_cast<int>(strlen("IFPFBits"));
        case 'N':
          VIXL_ASSERT(strncmp(format, "IFPNeon", strlen("IFPNeon")) == 0);
          imm8 = instr->GetImmNEONabcdefgh();
          len += strlen("Neon");
          break;
        case 'S':
          VIXL_ASSERT(strncmp(format, "IFPSve", strlen("IFPSve")) == 0);
          imm8 = instr->ExtractBits(12, 5);
          len += strlen("Sve");
          break;
        default:
          VIXL_ASSERT(strncmp(format, "IFP", strlen("IFP")) == 0);
          imm8 = instr->GetImmFP();
          break;
      }
      AppendToOutput("#0x%" PRIx32 " (%.4f)",
                     imm8,
                     Instruction::Imm8ToFP32(imm8));
      return static_cast<int>(len);
    }
    case 'H': {  // IH - ImmHint
      AppendToOutput("#%" PRId32, instr->GetImmHint());
      return 2;
    }
    case 'T': {  // ITri - Immediate Triangular Encoded.
      if (format[4] == 'S') {
        VIXL_ASSERT((format[5] == 'v') && (format[6] == 'e'));
        switch (format[7]) {
          case 'l':
            // SVE logical immediate encoding.
            AppendToOutput("#0x%" PRIx64, instr->GetSVEImmLogical());
            return 8;
          case 'p': {
            // SVE predicated shift immediate encoding, lsl.
            std::pair<int, int> shift_and_lane_size =
                instr->GetSVEImmShiftAndLaneSizeLog2(
                    /* is_predicated = */ true);
            int lane_bits = 8 << shift_and_lane_size.second;
            AppendToOutput("#%" PRId32, lane_bits - shift_and_lane_size.first);
            return 8;
          }
          case 'q': {
            // SVE predicated shift immediate encoding, asr and lsr.
            std::pair<int, int> shift_and_lane_size =
                instr->GetSVEImmShiftAndLaneSizeLog2(
                    /* is_predicated = */ true);
            AppendToOutput("#%" PRId32, shift_and_lane_size.first);
            return 8;
          }
          case 'r': {
            // SVE unpredicated shift immediate encoding, left shifts.
            std::pair<int, int> shift_and_lane_size =
                instr->GetSVEImmShiftAndLaneSizeLog2(
                    /* is_predicated = */ false);
            int lane_bits = 8 << shift_and_lane_size.second;
            AppendToOutput("#%" PRId32, lane_bits - shift_and_lane_size.first);
            return 8;
          }
          case 's': {
            // SVE unpredicated shift immediate encoding, right shifts.
            std::pair<int, int> shift_and_lane_size =
                instr->GetSVEImmShiftAndLaneSizeLog2(
                    /* is_predicated = */ false);
            AppendToOutput("#%" PRId32, shift_and_lane_size.first);
            return 8;
          }
          default:
            VIXL_UNREACHABLE();
            return 0;
        }
      } else {
        AppendToOutput("#0x%" PRIx64, instr->GetImmLogical());
        return 4;
      }
    }
    case 'N': {  // INzcv.
      int nzcv = (instr->GetNzcv() << Flags_offset);
      AppendToOutput("#%c%c%c%c",
                     ((nzcv & NFlag) == 0) ? 'n' : 'N',
                     ((nzcv & ZFlag) == 0) ? 'z' : 'Z',
                     ((nzcv & CFlag) == 0) ? 'c' : 'C',
                     ((nzcv & VFlag) == 0) ? 'v' : 'V');
      return 5;
    }
    case 'P': {  // IP - Conditional compare.
      AppendToOutput("#%" PRId32, instr->GetImmCondCmp());
      return 2;
    }
    case 'B': {  // Bitfields.
      return SubstituteBitfieldImmediateField(instr, format);
    }
    case 'E': {  // IExtract.
      AppendToOutput("#%" PRId32, instr->GetImmS());
      return 8;
    }
    case 't': {  // It - Test and branch bit.
      AppendToOutput("#%" PRId32,
                     (instr->GetImmTestBranchBit5() << 5) |
                         instr->GetImmTestBranchBit40());
      return 2;
    }
    case 'S': {  // ISveSvl - SVE 'mul vl' immediate for structured ld/st.
      VIXL_ASSERT(strncmp(format, "ISveSvl", 7) == 0);
      int imm = instr->ExtractSignedBits(19, 16);
      if (imm != 0) {
        int reg_count = instr->ExtractBits(22, 21) + 1;
        AppendToOutput(", #%d, mul vl", imm * reg_count);
      }
      return 7;
    }
    case 's': {  // Is - Shift (immediate).
      switch (format[2]) {
        case 'R': {  // IsR - right shifts.
          int shift = 16 << HighestSetBitPosition(instr->GetImmNEONImmh());
          shift -= instr->GetImmNEONImmhImmb();
          AppendToOutput("#%d", shift);
          return 3;
        }
        case 'L': {  // IsL - left shifts.
          int shift = instr->GetImmNEONImmhImmb();
          shift -= 8 << HighestSetBitPosition(instr->GetImmNEONImmh());
          AppendToOutput("#%d", shift);
          return 3;
        }
        default: {
          VIXL_UNIMPLEMENTED();
          return 0;
        }
      }
    }
    case 'D': {  // IDebug - HLT and BRK instructions.
      AppendToOutput("#0x%" PRIx32, instr->GetImmException());
      return 6;
    }
    case 'U': {  // IUdf - UDF immediate.
      AppendToOutput("#0x%" PRIx32, instr->GetImmUdf());
      return 4;
    }
    case 'V': {  // Immediate Vector.
      switch (format[2]) {
        case 'E': {  // IVExtract.
          AppendToOutput("#%" PRId32, instr->GetImmNEONExt());
          return 9;
        }
        case 'B': {  // IVByElemIndex.
          int ret = static_cast<int>(strlen("IVByElemIndex"));
          uint32_t vm_index = instr->GetNEONH() << 2;
          vm_index |= instr->GetNEONL() << 1;
          vm_index |= instr->GetNEONM();

          static const char *format_rot = "IVByElemIndexRot";
          static const char *format_fhm = "IVByElemIndexFHM";
          if (strncmp(format, format_rot, strlen(format_rot)) == 0) {
            // FCMLA uses 'H' bit index when SIZE is 2, else H:L
            VIXL_ASSERT((instr->GetNEONSize() == 1) ||
                        (instr->GetNEONSize() == 2));
            vm_index >>= instr->GetNEONSize();
            ret = static_cast<int>(strlen(format_rot));
          } else if (strncmp(format, format_fhm, strlen(format_fhm)) == 0) {
            // Nothing to do - FMLAL and FMLSL use H:L:M.
            ret = static_cast<int>(strlen(format_fhm));
          } else {
            if (instr->GetNEONSize() == 2) {
              // S-sized elements use H:L.
              vm_index >>= 1;
            } else if (instr->GetNEONSize() == 3) {
              // D-sized elements use H.
              vm_index >>= 2;
            }
          }
          AppendToOutput("%d", vm_index);
          return ret;
        }
        case 'I': {  // INS element.
          if (strncmp(format, "IVInsIndex", strlen("IVInsIndex")) == 0) {
            unsigned rd_index, rn_index;
            unsigned imm5 = instr->GetImmNEON5();
            unsigned imm4 = instr->GetImmNEON4();
            int tz = CountTrailingZeros(imm5, 32);
            if (tz <= 3) {  // Defined for tz = 0 to 3 only.
              rd_index = imm5 >> (tz + 1);
              rn_index = imm4 >> tz;
              if (strncmp(format, "IVInsIndex1", strlen("IVInsIndex1")) == 0) {
                AppendToOutput("%d", rd_index);
                return static_cast<int>(strlen("IVInsIndex1"));
              } else if (strncmp(format,
                                 "IVInsIndex2",
                                 strlen("IVInsIndex2")) == 0) {
                AppendToOutput("%d", rn_index);
                return static_cast<int>(strlen("IVInsIndex2"));
              }
            }
            return 0;
          } else if (strncmp(format,
                             "IVInsSVEIndex",
                             strlen("IVInsSVEIndex")) == 0) {
            std::pair<int, int> index_and_lane_size =
                instr->GetSVEPermuteIndexAndLaneSizeLog2();
            AppendToOutput("%d", index_and_lane_size.first);
            return static_cast<int>(strlen("IVInsSVEIndex"));
          }
          VIXL_FALLTHROUGH();
        }
        case 'L': {  // IVLSLane[0123] - suffix indicates access size shift.
          AppendToOutput("%d", instr->GetNEONLSIndex(format[8] - '0'));
          return 9;
        }
        case 'M': {  // Modified Immediate cases.
          if (strncmp(format, "IVMIImm", strlen("IVMIImm")) == 0) {
            uint64_t imm8 = instr->GetImmNEONabcdefgh();
            uint64_t imm = 0;
            for (int i = 0; i < 8; ++i) {
              if (imm8 & (UINT64_C(1) << i)) {
                imm |= (UINT64_C(0xff) << (8 * i));
              }
            }
            AppendToOutput("#0x%" PRIx64, imm);
            return static_cast<int>(strlen("IVMIImm"));
          } else {
            VIXL_UNIMPLEMENTED();
            return 0;
          }
        }
        default: {
          VIXL_UNIMPLEMENTED();
          return 0;
        }
      }
    }
    case 'X': {  // IX - CLREX instruction.
      AppendToOutput("#0x%" PRIx32, instr->GetCRm());
      return 2;
    }
    case 'Y': {  // IY - system register immediate.
      switch (instr->GetImmSystemRegister()) {
        case NZCV:
          AppendToOutput("nzcv");
          break;
        case FPCR:
          AppendToOutput("fpcr");
          break;
        case RNDR:
          AppendToOutput("rndr");
          break;
        case RNDRRS:
          AppendToOutput("rndrrs");
          break;
        case DCZID_EL0:
          AppendToOutput("dczid_el0");
          break;
        default:
          AppendToOutput("S%d_%d_c%d_c%d_%d",
                         instr->GetSysOp0(),
                         instr->GetSysOp1(),
                         instr->GetCRn(),
                         instr->GetCRm(),
                         instr->GetSysOp2());
          break;
      }
      return 2;
    }
    case 'R': {  // IR - Rotate right into flags.
      switch (format[2]) {
        case 'r': {  // IRr - Rotate amount.
          AppendToOutput("#%d", instr->GetImmRMIFRotation());
          return 3;
        }
        default: {
          VIXL_UNIMPLEMENTED();
          return 0;
        }
      }
    }
    case 'p': {  // Ipc - SVE predicate constraint specifier.
      VIXL_ASSERT(format[2] == 'c');
      unsigned pattern = instr->GetImmSVEPredicateConstraint();
      switch (pattern) {
        // VL1-VL8 are encoded directly.
        case SVE_VL1:
        case SVE_VL2:
        case SVE_VL3:
        case SVE_VL4:
        case SVE_VL5:
        case SVE_VL6:
        case SVE_VL7:
        case SVE_VL8:
          AppendToOutput("vl%u", pattern);
          break;
        // VL16-VL256 are encoded as log2(N) + c.
        case SVE_VL16:
        case SVE_VL32:
        case SVE_VL64:
        case SVE_VL128:
        case SVE_VL256:
          AppendToOutput("vl%u", 16 << (pattern - SVE_VL16));
          break;
        // Special cases.
        case SVE_POW2:
          AppendToOutput("pow2");
          break;
        case SVE_MUL4:
          AppendToOutput("mul4");
          break;
        case SVE_MUL3:
          AppendToOutput("mul3");
          break;
        case SVE_ALL:
          AppendToOutput("all");
          break;
        default:
          AppendToOutput("#0x%x", pattern);
          break;
      }
      return 3;
    }
    default: {
      VIXL_UNIMPLEMENTED();
      return 0;
    }
  }
}


int Disassembler::SubstituteBitfieldImmediateField(const Instruction *instr,
                                                   const char *format) {
  VIXL_ASSERT((format[0] == 'I') && (format[1] == 'B'));
  unsigned r = instr->GetImmR();
  unsigned s = instr->GetImmS();

  switch (format[2]) {
    case 'r': {  // IBr.
      AppendToOutput("#%d", r);
      return 3;
    }
    case 's': {  // IBs+1 or IBs-r+1.
      if (format[3] == '+') {
        AppendToOutput("#%d", s + 1);
        return 5;
      } else {
        VIXL_ASSERT(format[3] == '-');
        AppendToOutput("#%d", s - r + 1);
        return 7;
      }
    }
    case 'Z': {  // IBZ-r.
      VIXL_ASSERT((format[3] == '-') && (format[4] == 'r'));
      unsigned reg_size =
          (instr->GetSixtyFourBits() == 1) ? kXRegSize : kWRegSize;
      AppendToOutput("#%d", reg_size - r);
      return 5;
    }
    default: {
      VIXL_UNREACHABLE();
      return 0;
    }
  }
}


int Disassembler::SubstituteLiteralField(const Instruction *instr,
                                         const char *format) {
  VIXL_ASSERT(strncmp(format, "LValue", 6) == 0);
  USE(format);

  const void *address = instr->GetLiteralAddress<const void *>();
  switch (instr->Mask(LoadLiteralMask)) {
    case LDR_w_lit:
    case LDR_x_lit:
    case LDRSW_x_lit:
    case LDR_s_lit:
    case LDR_d_lit:
    case LDR_q_lit:
      AppendCodeRelativeDataAddressToOutput(instr, address);
      break;
    case PRFM_lit: {
      // Use the prefetch hint to decide how to print the address.
      switch (instr->GetPrefetchHint()) {
        case 0x0:  // PLD: prefetch for load.
        case 0x2:  // PST: prepare for store.
          AppendCodeRelativeDataAddressToOutput(instr, address);
          break;
        case 0x1:  // PLI: preload instructions.
          AppendCodeRelativeCodeAddressToOutput(instr, address);
          break;
        case 0x3:  // Unallocated hint.
          AppendCodeRelativeAddressToOutput(instr, address);
          break;
      }
      break;
    }
    default:
      VIXL_UNREACHABLE();
  }

  return 6;
}


int Disassembler::SubstituteShiftField(const Instruction *instr,
                                       const char *format) {
  VIXL_ASSERT(format[0] == 'N');
  VIXL_ASSERT(instr->GetShiftDP() <= 0x3);

  switch (format[1]) {
    case 'D': {  // NDP.
      VIXL_ASSERT(instr->GetShiftDP() != ROR);
      VIXL_FALLTHROUGH();
    }
    case 'L': {  // NLo.
      if (instr->GetImmDPShift() != 0) {
        const char *shift_type[] = {"lsl", "lsr", "asr", "ror"};
        AppendToOutput(", %s #%" PRId32,
                       shift_type[instr->GetShiftDP()],
                       instr->GetImmDPShift());
      }
      return 3;
    }
    case 'S': {  // NSveS (SVE structured load/store indexing shift).
      VIXL_ASSERT(strncmp(format, "NSveS", 5) == 0);
      int msz = instr->ExtractBits(24, 23);
      if (msz > 0) {
        AppendToOutput(", lsl #%d", msz);
      }
      return 5;
    }
    default:
      VIXL_UNIMPLEMENTED();
      return 0;
  }
}


int Disassembler::SubstituteConditionField(const Instruction *instr,
                                           const char *format) {
  VIXL_ASSERT(format[0] == 'C');
  const char *condition_code[] = {"eq",
                                  "ne",
                                  "hs",
                                  "lo",
                                  "mi",
                                  "pl",
                                  "vs",
                                  "vc",
                                  "hi",
                                  "ls",
                                  "ge",
                                  "lt",
                                  "gt",
                                  "le",
                                  "al",
                                  "nv"};
  int cond;
  switch (format[1]) {
    case 'B':
      cond = instr->GetConditionBranch();
      break;
    case 'I': {
      cond = InvertCondition(static_cast<Condition>(instr->GetCondition()));
      break;
    }
    default:
      cond = instr->GetCondition();
  }
  AppendToOutput("%s", condition_code[cond]);
  return 4;
}


int Disassembler::SubstitutePCRelAddressField(const Instruction *instr,
                                              const char *format) {
  VIXL_ASSERT((strcmp(format, "AddrPCRelByte") == 0) ||  // Used by `adr`.
              (strcmp(format, "AddrPCRelPage") == 0));   // Used by `adrp`.

  int64_t offset = instr->GetImmPCRel();

  // Compute the target address based on the effective address (after applying
  // code_address_offset). This is required for correct behaviour of adrp.
  const Instruction *base = instr + code_address_offset();
  if (format[9] == 'P') {
    offset *= kPageSize;
    base = AlignDown(base, kPageSize);
  }
  // Strip code_address_offset before printing, so we can use the
  // semantically-correct AppendCodeRelativeAddressToOutput.
  const void *target =
      reinterpret_cast<const void *>(base + offset - code_address_offset());

  AppendPCRelativeOffsetToOutput(instr, offset);
  AppendToOutput(" ");
  AppendCodeRelativeAddressToOutput(instr, target);
  return 13;
}


int Disassembler::SubstituteBranchTargetField(const Instruction *instr,
                                              const char *format) {
  VIXL_ASSERT(strncmp(format, "TImm", 4) == 0);

  int64_t offset = 0;
  switch (format[5]) {
    // BImmUncn - unconditional branch immediate.
    case 'n':
      offset = instr->GetImmUncondBranch();
      break;
    // BImmCond - conditional branch immediate.
    case 'o':
      offset = instr->GetImmCondBranch();
      break;
    // BImmCmpa - compare and branch immediate.
    case 'm':
      offset = instr->GetImmCmpBranch();
      break;
    // BImmTest - test and branch immediate.
    case 'e':
      offset = instr->GetImmTestBranch();
      break;
    default:
      VIXL_UNIMPLEMENTED();
  }
  offset *= static_cast<int>(kInstructionSize);
  const void *target_address = reinterpret_cast<const void *>(instr + offset);
  VIXL_STATIC_ASSERT(sizeof(*instr) == 1);

  AppendPCRelativeOffsetToOutput(instr, offset);
  AppendToOutput(" ");
  AppendCodeRelativeCodeAddressToOutput(instr, target_address);

  return 8;
}


int Disassembler::SubstituteExtendField(const Instruction *instr,
                                        const char *format) {
  VIXL_ASSERT(strncmp(format, "Ext", 3) == 0);
  VIXL_ASSERT(instr->GetExtendMode() <= 7);
  USE(format);

  const char *extend_mode[] =
      {"uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx"};

  // If rd or rn is SP, uxtw on 32-bit registers and uxtx on 64-bit
  // registers becomes lsl.
  if (((instr->GetRd() == kZeroRegCode) || (instr->GetRn() == kZeroRegCode)) &&
      (((instr->GetExtendMode() == UXTW) && (instr->GetSixtyFourBits() == 0)) ||
       (instr->GetExtendMode() == UXTX))) {
    if (instr->GetImmExtendShift() > 0) {
      AppendToOutput(", lsl #%" PRId32, instr->GetImmExtendShift());
    }
  } else {
    AppendToOutput(", %s", extend_mode[instr->GetExtendMode()]);
    if (instr->GetImmExtendShift() > 0) {
      AppendToOutput(" #%" PRId32, instr->GetImmExtendShift());
    }
  }
  return 3;
}


int Disassembler::SubstituteLSRegOffsetField(const Instruction *instr,
                                             const char *format) {
  VIXL_ASSERT(strncmp(format, "Offsetreg", 9) == 0);
  const char *extend_mode[] = {"undefined",
                               "undefined",
                               "uxtw",
                               "lsl",
                               "undefined",
                               "undefined",
                               "sxtw",
                               "sxtx"};
  USE(format);

  unsigned shift = instr->GetImmShiftLS();
  Extend ext = static_cast<Extend>(instr->GetExtendMode());
  char reg_type = ((ext == UXTW) || (ext == SXTW)) ? 'w' : 'x';

  unsigned rm = instr->GetRm();
  if (rm == kZeroRegCode) {
    AppendToOutput("%czr", reg_type);
  } else {
    AppendToOutput("%c%d", reg_type, rm);
  }

  // Extend mode UXTX is an alias for shift mode LSL here.
  if (!((ext == UXTX) && (shift == 0))) {
    AppendToOutput(", %s", extend_mode[ext]);
    if (shift != 0) {
      AppendToOutput(" #%d", instr->GetSizeLS());
    }
  }
  return 9;
}


int Disassembler::SubstitutePrefetchField(const Instruction *instr,
                                          const char *format) {
  VIXL_ASSERT(format[0] == 'p');
  USE(format);

  bool is_sve =
      (strncmp(format, "prefSVEOp", strlen("prefSVEOp")) == 0) ? true : false;
  int placeholder_length = is_sve ? 9 : 6;
  static const char *stream_options[] = {"keep", "strm"};

  auto get_hints = [](bool want_sve_hint) -> std::vector<std::string> {
    static const std::vector<std::string> sve_hints = {"ld", "st"};
    static const std::vector<std::string> core_hints = {"ld", "li", "st"};
    return (want_sve_hint) ? sve_hints : core_hints;
  };

  std::vector<std::string> hints = get_hints(is_sve);
  unsigned hint =
      is_sve ? instr->GetSVEPrefetchHint() : instr->GetPrefetchHint();
  unsigned target = instr->GetPrefetchTarget() + 1;
  unsigned stream = instr->GetPrefetchStream();

  if ((hint >= hints.size()) || (target > 3)) {
    // Unallocated prefetch operations.
    if (is_sve) {
      std::bitset<4> prefetch_mode(instr->GetSVEImmPrefetchOperation());
      AppendToOutput("#0b%s", prefetch_mode.to_string().c_str());
    } else {
      std::bitset<5> prefetch_mode(instr->GetImmPrefetchOperation());
      AppendToOutput("#0b%s", prefetch_mode.to_string().c_str());
    }
  } else {
    VIXL_ASSERT(stream < ArrayLength(stream_options));
    AppendToOutput("p%sl%d%s",
                   hints[hint].c_str(),
                   target,
                   stream_options[stream]);
  }
  return placeholder_length;
}

int Disassembler::SubstituteBarrierField(const Instruction *instr,
                                         const char *format) {
  VIXL_ASSERT(format[0] == 'M');
  USE(format);

  static const char *options[4][4] = {{"sy (0b0000)", "oshld", "oshst", "osh"},
                                      {"sy (0b0100)", "nshld", "nshst", "nsh"},
                                      {"sy (0b1000)", "ishld", "ishst", "ish"},
                                      {"sy (0b1100)", "ld", "st", "sy"}};
  int domain = instr->GetImmBarrierDomain();
  int type = instr->GetImmBarrierType();

  AppendToOutput("%s", options[domain][type]);
  return 1;
}

int Disassembler::SubstituteSysOpField(const Instruction *instr,
                                       const char *format) {
  VIXL_ASSERT(format[0] == 'G');
  int op = -1;
  switch (format[1]) {
    case '1':
      op = instr->GetSysOp1();
      break;
    case '2':
      op = instr->GetSysOp2();
      break;
    default:
      VIXL_UNREACHABLE();
  }
  AppendToOutput("#%d", op);
  return 2;
}

int Disassembler::SubstituteCrField(const Instruction *instr,
                                    const char *format) {
  VIXL_ASSERT(format[0] == 'K');
  int cr = -1;
  switch (format[1]) {
    case 'n':
      cr = instr->GetCRn();
      break;
    case 'm':
      cr = instr->GetCRm();
      break;
    default:
      VIXL_UNREACHABLE();
  }
  AppendToOutput("C%d", cr);
  return 2;
}

int Disassembler::SubstituteIntField(const Instruction *instr,
                                     const char *format) {
  VIXL_ASSERT((format[0] == 'u') || (format[0] == 's') || (format[0] == 'x'));

  // A generic signed or unsigned int field uses a placeholder of the form
  // 'sAABB and 'uAABB respectively where AA and BB are two digit bit positions
  // between 00 and 31, and AA >= BB. The placeholder is substituted with the
  // decimal integer represented by the bits in the instruction between
  // positions AA and BB inclusive.
  //
  // In addition, split fields can be represented using 'sAABB:CCDD, where CCDD
  // become the least-significant bits of the result, and bit AA is the sign bit
  // (if 's is used).
  //
  // For unsigned fields, 'u may be replaced with 'x to substitute the
  // hexadecimal representation instead of a decimal.
  int32_t bits = 0;
  int width = 0;
  const char *c = format;
  do {
    c++;  // Skip the 'u', 's', 'x' or ':'.
    VIXL_ASSERT(strspn(c, "0123456789") == 4);
    int msb = ((c[0] - '0') * 10) + (c[1] - '0');
    int lsb = ((c[2] - '0') * 10) + (c[3] - '0');
    c += 4;  // Skip the characters we just read.
    int chunk_width = msb - lsb + 1;
    VIXL_ASSERT((chunk_width > 0) && (chunk_width < 32));
    bits = (bits << chunk_width) | (instr->ExtractBits(msb, lsb));
    width += chunk_width;
  } while (*c == ':');
  VIXL_ASSERT(IsUintN(width, bits));

  if (format[0] == 's') {
    bits = ExtractSignedBitfield32(width - 1, 0, bits);
  }

  if (*c == '+') {
    // A "+n" trailing the format specifier indicates the extracted value should
    // be incremented by n. This is for cases where the encoding is zero-based,
    // but range of values is not, eg. values [1, 16] encoded as [0, 15]
    char *new_c;
    uint64_t value = strtoul(c + 1, &new_c, 10);
    c = new_c;
    VIXL_ASSERT(IsInt32(value));
    bits = static_cast<int32_t>(bits + value);
  } else if (*c == '*') {
    // Similarly, a "*n" trailing the format specifier indicates the extracted
    // value should be multiplied by n. This is for cases where the encoded
    // immediate is scaled, for example by access size.
    char *new_c;
    uint64_t value = strtoul(c + 1, &new_c, 10);
    c = new_c;
    VIXL_ASSERT(IsInt32(value));
    bits = static_cast<int32_t>(bits * value);
  }

  AppendToOutput(format[0] == 'x' ? "%x" : "%d", bits);

  return static_cast<int>(c - format);
}

int Disassembler::SubstituteSVESize(const Instruction *instr,
                                    const char *format) {
  USE(format);
  VIXL_ASSERT(format[0] == 't');

  static const char sizes[] = {'b', 'h', 's', 'd', 'q'};
  unsigned size_in_bytes_log2 = instr->GetSVESize();
  int placeholder_length = 1;
  switch (format[1]) {
    case 'f':  // 'tf - FP size encoded in <18:17>
      placeholder_length++;
      size_in_bytes_log2 = instr->ExtractBits(18, 17);
      break;
    case 'l':
      placeholder_length++;
      if (format[2] == 's') {
        // 'tls: Loads and stores
        size_in_bytes_log2 = instr->ExtractBits(22, 21);
        placeholder_length++;
        if (format[3] == 's') {
          // Sign extension load.
          unsigned msize = instr->ExtractBits(24, 23);
          if (msize > size_in_bytes_log2) size_in_bytes_log2 ^= 0x3;
          placeholder_length++;
        }
      } else {
        // 'tl: Logical operations
        size_in_bytes_log2 = instr->GetSVEBitwiseImmLaneSizeInBytesLog2();
      }
      break;
    case 'm':  // 'tmsz
      VIXL_ASSERT(strncmp(format, "tmsz", 4) == 0);
      placeholder_length += 3;
      size_in_bytes_log2 = instr->ExtractBits(24, 23);
      break;
    case 'i': {  // 'ti: indices.
      std::pair<int, int> index_and_lane_size =
          instr->GetSVEPermuteIndexAndLaneSizeLog2();
      placeholder_length++;
      size_in_bytes_log2 = index_and_lane_size.second;
      break;
    }
    case 's':
      if (format[2] == 'z') {
        VIXL_ASSERT((format[3] == 'p') || (format[3] == 's') ||
                    (format[3] == 'd'));
        bool is_predicated = (format[3] == 'p');
        std::pair<int, int> shift_and_lane_size =
            instr->GetSVEImmShiftAndLaneSizeLog2(is_predicated);
        size_in_bytes_log2 = shift_and_lane_size.second;
        if (format[3] == 'd') {  // Double size lanes.
          size_in_bytes_log2++;
        }
        placeholder_length += 3;  // skip "sz(p|s|d)"
      }
      break;
    case 'h':
      // Half size of the lane size field.
      size_in_bytes_log2 -= 1;
      placeholder_length++;
      break;
    case 'q':
      // Quarter size of the lane size field.
      size_in_bytes_log2 -= 2;
      placeholder_length++;
      break;
    default:
      break;
  }

  VIXL_ASSERT(size_in_bytes_log2 < ArrayLength(sizes));
  AppendToOutput("%c", sizes[size_in_bytes_log2]);

  return placeholder_length;
}

int Disassembler::SubstituteTernary(const Instruction *instr,
                                    const char *format) {
  VIXL_ASSERT((format[0] == '?') && (format[3] == ':'));

  // The ternary substitution of the format "'?bb:TF" is replaced by a single
  // character, either T or F, depending on the value of the bit at position
  // bb in the instruction. For example, "'?31:xw" is substituted with "x" if
  // bit 31 is true, and "w" otherwise.
  VIXL_ASSERT(strspn(&format[1], "0123456789") == 2);
  char *c;
  uint64_t value = strtoul(&format[1], &c, 10);
  VIXL_ASSERT(value < (kInstructionSize * kBitsPerByte));
  VIXL_ASSERT((*c == ':') && (strlen(c) >= 3));  // Minimum of ":TF"
  c++;
  AppendToOutput("%c", c[1 - instr->ExtractBit(static_cast<int>(value))]);
  return 6;
}

int Disassembler::SubstituteConditionalBlock(const Instruction *instr,
                                             const char *format) {
  VIXL_ASSERT(strlen(format) >= 6);
  VIXL_ASSERT((format[0] == '(') && (format[5] == '?' || (format[5] == '=')));
  VIXL_ASSERT(strchr(format, ')') != nullptr);

  // A conditional block uses the placeholder '(AABB?xxx:yyyy)' where AA and
  // BB are two digit bit positions between 00 and 31, and AA >= BB. If the
  // bits of the instruction in the range AA to BB are non-zero, the placeholder
  // is substituted with the string represented by xxx, else yyyy. The strings
  // are of variable length and may contain other placeholders for further
  // substitutions. The ':yyyy' section may be omitted, implying a zero-length
  // string is substituted if instruction bits in the range AA to BB are zero.
  //
  // Alternatively, a specific value for the bits in the range AA to BB can
  // be specified using the placeholder '(AABB=zzz?xxx:yyyy)'. If the bits in
  // the range AA to BB are equal to zzz, xxx is substitued, else yyyy. As
  // above, :yyyy may be omitted.
  VIXL_ASSERT(strspn(&format[1], "0123456789") == 4);
  const char *c = &format[1];
  int msb = ((c[0] - '0') * 10) + (c[1] - '0');
  int lsb = ((c[2] - '0') * 10) + (c[3] - '0');
  uint32_t bits = instr->ExtractBits(msb, lsb);
  uint64_t value = 0;
  bool use_explicit_value = false;

  if (format[5] == '=') {
    use_explicit_value = true;
    char *temp;
    VIXL_ASSERT(strspn(&format[6], "0123456789") > 0);
    value = strtoul(&format[6], &temp, 10);
    c = temp;
  } else {
    c += 4;  // Skip the bit positions we read above.
  }

  // Skip '?'
  VIXL_ASSERT(*c == '?');
  c++;

  char temp[256] = {0};
  const char *close = strchr(format, ')');
  size_t subst_len = close - c;
  VIXL_ASSERT(subst_len < sizeof(temp));

  // Copy the substitution string into a temporary buffer and set up pointers
  // for the left-hand (true) and right-hand (false) sides.
  memcpy(temp, c, subst_len);

  char *lhs = temp;
  char *rhs = nullptr;
  char *colon = strchr(temp, ':');
  if (colon != nullptr) {
    // If there's a colon, set it to zero to act as the terminator for the
    // left-hand string.
    *colon = 0;
    rhs = colon + 1;
  }

  bool use_lhs;
  if (use_explicit_value) {
    use_lhs = (bits == value);
  } else {
    use_lhs = (bits != 0);
  }

  char *subst = use_lhs ? lhs : rhs;
  if ((subst != nullptr) && (strlen(subst) > 0)) {
    Substitute(instr, subst);
  }

  return static_cast<int>(1 + close - format);
}

void Disassembler::ResetOutput() {
  buffer_pos_ = 0;
  buffer_[buffer_pos_] = 0;
}


void Disassembler::AppendToOutput(const char *format, ...) {
  va_list args;
  va_start(args, format);
  buffer_pos_ += vsnprintf(&buffer_[buffer_pos_],
                           buffer_size_ - buffer_pos_,
                           format,
                           args);
  va_end(args);
}


void PrintDisassembler::Disassemble(const Instruction *instr) {
  Decoder decoder;
  if (cpu_features_auditor_ != NULL) {
    decoder.AppendVisitor(cpu_features_auditor_);
  }
  decoder.AppendVisitor(this);
  decoder.Decode(instr);
}

void PrintDisassembler::DisassembleBuffer(const Instruction *start,
                                          const Instruction *end) {
  Decoder decoder;
  if (cpu_features_auditor_ != NULL) {
    decoder.AppendVisitor(cpu_features_auditor_);
  }
  decoder.AppendVisitor(this);
  decoder.Decode(start, end);
}

void PrintDisassembler::DisassembleBuffer(const Instruction *start,
                                          uint64_t size) {
  DisassembleBuffer(start, start + size);
}


void PrintDisassembler::ProcessOutput(const Instruction *instr) {
  int64_t address = CodeRelativeAddress(instr);

  uint64_t abs_address;
  const char *sign;
  if (signed_addresses_) {
    if (address < 0) {
      sign = "-";
      abs_address = UnsignedNegate(static_cast<uint64_t>(address));
    } else {
      // Leave a leading space, to maintain alignment.
      sign = " ";
      abs_address = address;
    }
  } else {
    sign = "";
    abs_address = address;
  }

  int bytes_printed = fprintf(stream_,
                              "%s0x%016" PRIx64 "  %08" PRIx32 "\t\t%s",
                              sign,
                              abs_address,
                              instr->GetInstructionBits(),
                              GetOutput());
  if (cpu_features_auditor_ != NULL) {
    CPUFeatures needs = cpu_features_auditor_->GetInstructionFeatures();
    needs.Remove(cpu_features_auditor_->GetAvailableFeatures());
    if (needs != CPUFeatures::None()) {
      // Try to align annotations. This value is arbitrary, but based on looking
      // good with most instructions. Note that, for historical reasons, the
      // disassembly itself is printed with tab characters, so bytes_printed is
      // _not_ equivalent to the number of occupied screen columns. However, the
      // prefix before the tabs is always the same length, so the annotation
      // indentation does not change from one line to the next.
      const int indent_to = 70;
      // Always allow some space between the instruction and the annotation.
      const int min_pad = 2;

      int pad = std::max(min_pad, (indent_to - bytes_printed));
      fprintf(stream_, "%*s", pad, "");

      std::stringstream features;
      features << needs;
      fprintf(stream_,
              "%s%s%s",
              cpu_features_prefix_,
              features.str().c_str(),
              cpu_features_suffix_);
    }
  }
  fprintf(stream_, "\n");
}

}  // namespace aarch64
}  // namespace vixl
