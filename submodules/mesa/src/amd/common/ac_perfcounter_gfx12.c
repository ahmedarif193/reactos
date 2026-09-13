/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_perfcounter.h"
#include "ac_spm.h"

/* CB */
static unsigned gfx12_CB_select0[] = {
   R_037004_CB_PERFCOUNTER0_SELECT,
   R_03700C_CB_PERFCOUNTER1_SELECT,
   R_037010_CB_PERFCOUNTER2_SELECT,
   R_037014_CB_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_CB_select1[] = {
   R_037008_CB_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx12_CB = {
   .gpu_block = CB,
   .name = "CB",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = gfx12_CB_select0,
   .select1 = gfx12_CB_select1,
   .counter0_lo = R_035018_CB_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_CB,
};

/* CPC */
static unsigned gfx12_CPC_select0[] = {
   R_036024_CPC_PERFCOUNTER0_SELECT,
   R_03600C_CPC_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_CPC_select1[] = {
   R_036010_CPC_PERFCOUNTER0_SELECT1,
};
static unsigned gfx12_CPC_counters[] = {
   R_034018_CPC_PERFCOUNTER0_LO,
   R_034010_CPC_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx12_CPC = {
   .gpu_block = CPC,
   .name = "CPC",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_CPC_select0,
   .select1 = gfx12_CPC_select1,
   .counters = gfx12_CPC_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPC,
};

/* CPF */
static unsigned gfx12_CPF_select0[] = {
   R_03601C_CPF_PERFCOUNTER0_SELECT,
   R_036014_CPF_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_CPF_select1[] = {
   R_036018_CPF_PERFCOUNTER0_SELECT1,
};
static unsigned gfx12_CPF_counters[] = {
   R_034028_CPF_PERFCOUNTER0_LO,
   R_034020_CPF_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx12_CPF = {
   .gpu_block = CPF,
   .name = "CPF",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_CPF_select0,
   .select1 = gfx12_CPF_select1,
   .counters = gfx12_CPF_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPF,
};

/* CPG */
static unsigned gfx12_CPG_select0[] = {
   R_036008_CPG_PERFCOUNTER0_SELECT,
   R_036000_CPG_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_CPG_select1[] = {
   R_036004_CPG_PERFCOUNTER0_SELECT1
};
static unsigned gfx12_CPG_counters[] = {
   R_034008_CPG_PERFCOUNTER0_LO,
   R_034000_CPG_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx12_CPG = {
   .gpu_block = CPG,
   .name = "CPG",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_CPG_select0,
   .select1 = gfx12_CPG_select1,
   .counters = gfx12_CPG_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPG,
};

/* GRBM */
static unsigned gfx12_GRBM_select0[] = {
   R_036100_GRBM_PERFCOUNTER0_SELECT,
   R_036104_GRBM_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_GRBM_counters[] = {
   R_034100_GRBM_PERFCOUNTER0_LO,
   R_03410C_GRBM_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx12_GRBM = {
   .gpu_block = GRBM,
   .name = "GRBM",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_GRBM_select0,
   .counters = gfx12_GRBM_counters,
};

/* PA_SC */
static unsigned gfx12_PA_SC_select0[] = {
   R_036500_PA_SC_PERFCOUNTER0_SELECT,
   R_036508_PA_SC_PERFCOUNTER1_SELECT,
   R_03650C_PA_SC_PERFCOUNTER2_SELECT,
   R_036510_PA_SC_PERFCOUNTER3_SELECT,
   R_036514_PA_SC_PERFCOUNTER4_SELECT,
   R_036518_PA_SC_PERFCOUNTER5_SELECT,
   R_03651C_PA_SC_PERFCOUNTER6_SELECT,
   R_036520_PA_SC_PERFCOUNTER7_SELECT,
};
static unsigned gfx12_PA_SC_select1[] = {
   R_036504_PA_SC_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx12_PA_SC = {
   .gpu_block = PA_SC,
   .name = "PA_SC",
   .num_counters = 8,
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx12_PA_SC_select0,
   .select1 = gfx12_PA_SC_select1,
   .counter0_lo = R_034500_PA_SC_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_SC,
};

/* SPI */
static unsigned gfx12_SPI_select0[] = {
   R_036600_SPI_PERFCOUNTER0_SELECT,
   R_036604_SPI_PERFCOUNTER1_SELECT,
   R_036608_SPI_PERFCOUNTER2_SELECT,
   R_03660C_SPI_PERFCOUNTER3_SELECT,
   R_036610_SPI_PERFCOUNTER4_SELECT,
   R_036614_SPI_PERFCOUNTER5_SELECT,
};
static unsigned gfx12_SPI_select1[] = {
   R_036618_SPI_PERFCOUNTER0_SELECT1,
   R_03661C_SPI_PERFCOUNTER1_SELECT1,
   R_036620_SPI_PERFCOUNTER2_SELECT1,
   R_036624_SPI_PERFCOUNTER3_SELECT1,
   R_036628_SPI_PERFCOUNTER4_SELECT1,
   R_03662C_SPI_PERFCOUNTER5_SELECT1
};
static struct ac_pc_block_base gfx12_SPI = {
   .gpu_block = SPI,
   .name = "SPI",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 6,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx12_SPI_select0,
   .select1 = gfx12_SPI_select1,
   .counter0_lo = R_034604_SPI_PERFCOUNTER0_LO,

   .num_spm_modules = 6,
   .num_spm_wires = 12,
   .spm_block_select = AC_SPM_SE_BLOCK_SPI,
};

/* SX */
static unsigned gfx12_SX_select0[] = {
   R_036900_SX_PERFCOUNTER0_SELECT,
   R_036904_SX_PERFCOUNTER1_SELECT,
   R_036908_SX_PERFCOUNTER2_SELECT,
   R_03690C_SX_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_SX_select1[] = {
   R_036910_SX_PERFCOUNTER0_SELECT1,
   R_036914_SX_PERFCOUNTER1_SELECT1,
   R_036918_SX_PERFCOUNTER2_SELECT1,
   R_03691C_SX_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_SX = {
   .gpu_block = SX,
   .name = "SX",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx12_SX_select0,
   .select1 = gfx12_SX_select1,
   .counter0_lo = R_034900_SX_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_SX,
};

/* TA */
static unsigned gfx12_TA_select0[] = {
   R_036B00_TA_PERFCOUNTER0_SELECT,
   R_036B08_TA_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_TA_select1[] = {
   R_036B04_TA_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx12_TA = {
   .gpu_block = TA,
   .name = "TA",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx12_TA_select0,
   .select1 = gfx12_TA_select1,
   .counter0_lo = R_034B00_TA_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_TA,
};

/* TD */
static unsigned gfx12_TD_select0[] = {
   R_036C00_TD_PERFCOUNTER0_SELECT,
   R_036C08_TD_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_TD_select1[] = {
   R_036C04_TD_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx12_TD = {
   .gpu_block = TD,
   .name = "TD",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx12_TD_select0,
   .select1 = gfx12_TD_select1,
   .counter0_lo = R_034C00_TD_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_TD,
};

/* CHA */
static unsigned gfx12_CHA_select0[] = {
   R_037780_CHA_PERFCOUNTER0_SELECT,
   R_037788_CHA_PERFCOUNTER1_SELECT,
   R_037790_CHA_PERFCOUNTER2_SELECT,
   R_037798_CHA_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_CHA_select1[] = {
   R_037784_CHA_PERFCOUNTER0_SELECT1,
   R_03778C_CHA_PERFCOUNTER1_SELECT1,
   R_037794_CHA_PERFCOUNTER2_SELECT1,
   R_03779C_CHA_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_CHA = {
   .gpu_block = CHA,
   .name = "CHA",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx12_CHA_select0,
   .select1 = gfx12_CHA_select1,
   .counter0_lo = R_035800_CHA_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CHA,
};

/* CHC */
static unsigned gfx12_CHC_select0[] = {
   R_036F00_CHC_PERFCOUNTER0_SELECT,
   R_036F08_CHC_PERFCOUNTER1_SELECT,
   R_036F10_CHC_PERFCOUNTER2_SELECT,
   R_036F18_CHC_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_CHC_select1[] = {
   R_036F04_CHC_PERFCOUNTER0_SELECT1,
   R_036F0C_CHC_PERFCOUNTER1_SELECT1,
   R_036F14_CHC_PERFCOUNTER2_SELECT1,
   R_036F1C_CHC_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_CHC = {
   .gpu_block = CHC,
   .name = "CHC",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx12_CHC_select0,
   .select1 = gfx12_CHC_select1,
   .counter0_lo = R_034F00_CHC_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CHC,
};

/* DB */
static unsigned gfx12_DB_select0[] = {
   R_037100_DB_PERFCOUNTER0_SELECT,
   R_037108_DB_PERFCOUNTER1_SELECT,
   R_037110_DB_PERFCOUNTER2_SELECT,
   R_037118_DB_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_DB_select1[] = {
   R_037104_DB_PERFCOUNTER0_SELECT1,
   R_03710C_DB_PERFCOUNTER1_SELECT1,
   R_037114_DB_PERFCOUNTER2_SELECT1,
   R_03711C_DB_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_DB = {
   .gpu_block = DB,
   .name = "DB",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = gfx12_DB_select0,
   .select1 = gfx12_DB_select1,
   .counter0_lo = R_035100_DB_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_DB,
};

/* GCR */
static unsigned gfx12_GCR_select0[] = {
   R_037580_GCR_PERFCOUNTER0_SELECT,
   R_037588_GCR_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_GCR_select1[] = {
   R_037584_GCR_PERFCOUNTER0_SELECT1,
   R_03758C_GCR_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx12_GCR = {
   .gpu_block = GCR,
   .name = "GCR",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_GCR_select0,
   .select1 = gfx12_GCR_select1,
   .counter0_lo = R_035480_GCR_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GCR,
};

/* GE */
static unsigned gfx12_GE_select0[] = {
   R_036290_GE1_PERFCOUNTER0_SELECT,
   R_036298_GE1_PERFCOUNTER1_SELECT,
   R_0362A0_GE1_PERFCOUNTER2_SELECT,
   R_0362A8_GE1_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GE_select1[] = {
   R_036294_GE1_PERFCOUNTER0_SELECT1,
   R_03629C_GE1_PERFCOUNTER1_SELECT1,
   R_0362A4_GE1_PERFCOUNTER2_SELECT1,
   R_0362AC_GE1_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GE = {
   .gpu_block = GE,
   .name = "GE",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx12_GE_select0,
   .select1 = gfx12_GE_select1,
   .counter0_lo = R_034290_GE1_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GE,
};

/* GL1A */
static unsigned gfx12_GL1A_select0[] = {
   R_037700_GL1A_PERFCOUNTER0_SELECT,
   R_037708_GL1A_PERFCOUNTER1_SELECT,
   R_037710_GL1A_PERFCOUNTER2_SELECT,
   R_037718_GL1A_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GL1A_select1[] = {
   R_037704_GL1A_PERFCOUNTER0_SELECT1,
   R_03770C_GL1A_PERFCOUNTER1_SELECT1,
   R_037714_GL1A_PERFCOUNTER2_SELECT1,
   R_03771C_GL1A_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GL1A = {
   .gpu_block = GL1A,
   .name = "GL1A",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx12_GL1A_select0,
   .select1 = gfx12_GL1A_select1,
   .counter0_lo = R_035700_GL1A_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_GL1A,
};

/* GL1C */
static unsigned gfx12_GL1C_select0[] = {
   R_036E80_GL1C_PERFCOUNTER0_SELECT,
   R_036E88_GL1C_PERFCOUNTER1_SELECT,
   R_036E90_GL1C_PERFCOUNTER2_SELECT,
   R_036E98_GL1C_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GL1C_select1[] = {
   R_036E84_GL1C_PERFCOUNTER0_SELECT1,
   R_036E8C_GL1C_PERFCOUNTER1_SELECT1,
   R_036E94_GL1C_PERFCOUNTER2_SELECT1,
   R_036E9C_GL1C_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GL1C = {
   .gpu_block = GL1C,
   .name = "GL1C",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx12_GL1C_select0,
   .select1 = gfx12_GL1C_select1,
   .counter0_lo = R_034E80_GL1C_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_GL1C,
};

/* GL2A */
static unsigned gfx12_GL2A_select0[] = {
   R_036E40_GL2A_PERFCOUNTER0_SELECT,
   R_036E48_GL2A_PERFCOUNTER1_SELECT,
   R_036E50_GL2A_PERFCOUNTER2_SELECT,
   R_036E58_GL2A_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GL2A_select1[] = {
   R_036E44_GL2A_PERFCOUNTER0_SELECT1,
   R_036E4C_GL2A_PERFCOUNTER1_SELECT1,
   R_036E54_GL2A_PERFCOUNTER2_SELECT1,
   R_036E5C_GL2A_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GL2A = {
   .gpu_block = GL2A,
   .name = "GL2A",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx12_GL2A_select0,
   .select1 = gfx12_GL2A_select1,
   .counter0_lo = R_034E40_GL2A_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GL2A,
};

/* GL2C */
static unsigned gfx12_GL2C_select0[] = {
   R_036E00_GL2C_PERFCOUNTER0_SELECT,
   R_036E08_GL2C_PERFCOUNTER1_SELECT,
   R_036E10_GL2C_PERFCOUNTER2_SELECT,
   R_036E18_GL2C_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GL2C_select1[] = {
   R_036E04_GL2C_PERFCOUNTER0_SELECT1,
   R_036E0C_GL2C_PERFCOUNTER1_SELECT1,
   R_036E14_GL2C_PERFCOUNTER2_SELECT1,
   R_036E1C_GL2C_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GL2C = {
   .gpu_block = GL2C,
   .name = "GL2C",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx12_GL2C_select0,
   .select1 = gfx12_GL2C_select1,
   .counter0_lo = R_034E00_GL2C_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GL2C,
};

/* PA_SU */
static unsigned gfx12_PA_SU_select0[] = {
   R_036400_PA_SU_PERFCOUNTER0_SELECT,
   R_036408_PA_SU_PERFCOUNTER1_SELECT,
   R_036410_PA_SU_PERFCOUNTER2_SELECT,
   R_036418_PA_SU_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_PA_SU_select1[] = {
   R_036404_PA_SU_PERFCOUNTER0_SELECT1,
   R_03640C_PA_SU_PERFCOUNTER1_SELECT1,
   R_036414_PA_SU_PERFCOUNTER2_SELECT1,
   R_03641C_PA_SU_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_PA_SU = {
   .gpu_block = PA_SU,
   .name = "PA_SU",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx12_PA_SU_select0,
   .select1 = gfx12_PA_SU_select1,
   .counter0_lo = R_034400_PA_SU_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_PA,
};

/* RLC */
static unsigned gfx12_RLC_select0[] = {
   R_037304_RLC_PERFCOUNTER0_SELECT,
   R_037308_RLC_PERFCOUNTER1_SELECT,
};
static struct ac_pc_block_base gfx12_RLC = {
   .gpu_block = RLC,
   .name = "RLC",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_RLC_select0,
   .counter0_lo = R_035200_RLC_PERFCOUNTER0_LO,
};

/* SQ */
static unsigned gfx12_SQ_select0[] = {
   R_036740_SQG_PERFCOUNTER0_SELECT,
   R_036744_SQG_PERFCOUNTER1_SELECT,
   R_036748_SQG_PERFCOUNTER2_SELECT,
   R_03674C_SQG_PERFCOUNTER3_SELECT,
   R_036750_SQG_PERFCOUNTER4_SELECT,
   R_036754_SQG_PERFCOUNTER5_SELECT,
   R_036758_SQG_PERFCOUNTER6_SELECT,
   R_03675C_SQG_PERFCOUNTER7_SELECT,
};
static struct ac_pc_block_base gfx12_SQ = {
   .gpu_block = SQ,
   .name = "SQ",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 8,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER,

   .select0 = gfx12_SQ_select0,
   .select_or = S_036700_SQC_BANK_MASK(15),
   .counter0_lo = R_034790_SQG_PERFCOUNTER0_LO,

   .num_16bit_spm_counters = 0,
   .num_32bit_spm_counters = 8, /* Counters must be used as 32-bit */
   .num_spm_modules = 8,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_SQG,
};

/* TCP */
static unsigned gfx12_TCP_select0[] = {
   R_036D00_TCP_PERFCOUNTER0_SELECT,
   R_036D08_TCP_PERFCOUNTER1_SELECT,
   R_036D10_TCP_PERFCOUNTER2_SELECT,
   R_036D14_TCP_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_TCP_select1[] = {
   R_036D04_TCP_PERFCOUNTER0_SELECT1,
   R_036D0C_TCP_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx12_TCP = {
   .gpu_block = TCP,
   .name = "TCP",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx12_TCP_select0,
   .select1 = gfx12_TCP_select1,
   .counter0_lo = R_034D00_TCP_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_SE_BLOCK_TCP,
};

/* UTCL1 */
static unsigned gfx12_UTCL1_select0[] = {
   R_037680_UTCL1_PERFCOUNTER0_SELECT,
   R_037684_UTCL1_PERFCOUNTER1_SELECT,
   R_037688_UTCL1_PERFCOUNTER2_SELECT,
   R_03768C_UTCL1_PERFCOUNTER3_SELECT,
};
static struct ac_pc_block_base gfx12_UTCL1 = {
   .gpu_block = UTCL1,
   .name = "UTCL1",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx12_UTCL1_select0,
   .counter0_lo = R_035680_UTCL1_PERFCOUNTER0_LO,
};

/* SQ_WQP */
static unsigned gfx12_SQ_WGP_select0[] = {
   R_036700_SQ_PERFCOUNTER0_SELECT,
   R_036704_SQ_PERFCOUNTER1_SELECT,
   R_036708_SQ_PERFCOUNTER2_SELECT,
   R_03670C_SQ_PERFCOUNTER3_SELECT,
   R_036710_SQ_PERFCOUNTER4_SELECT,
   R_036714_SQ_PERFCOUNTER5_SELECT,
   R_036718_SQ_PERFCOUNTER6_SELECT,
   R_03671C_SQ_PERFCOUNTER7_SELECT,
   R_036720_SQ_PERFCOUNTER8_SELECT,
   R_036724_SQ_PERFCOUNTER9_SELECT,
   R_036728_SQ_PERFCOUNTER10_SELECT,
   R_03672C_SQ_PERFCOUNTER11_SELECT,
   R_036730_SQ_PERFCOUNTER12_SELECT,
   R_036734_SQ_PERFCOUNTER13_SELECT,
   R_036738_SQ_PERFCOUNTER14_SELECT,
   R_03673C_SQ_PERFCOUNTER15_SELECT,
};
static struct ac_pc_block_base gfx12_SQ_WGP = {
   .gpu_block = SQ_WGP,
   .name = "SQ_WGP",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 16,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER,

   .select0 = gfx12_SQ_WGP_select0,
   .counter0_lo = R_034700_SQ_PERFCOUNTER0_LO,

   .num_16bit_spm_counters = 16,
   .num_32bit_spm_counters = 8,
   .num_spm_modules = 8,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_SQC,
};

/* GRBMSE */
static unsigned gfx12_GRBMSE_select0[] = {
   R_0363E0_GRBMH_PERFCOUNTER0_SELECT,
   R_0363E4_GRBMH_PERFCOUNTER1_SELECT,
};

static struct ac_pc_block_base gfx12_GRBMSE = {
   .gpu_block = GRBMSE,
   .name = "GRBMSE",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER,

   .select0 = gfx12_GRBMSE_select0,
   .counter0_lo = R_0343E8_GRBMH_PERFCOUNTER0_LO,
};

/* GCEA_CPWD */
static unsigned gfx12_GCEA_CPWD_select0[] = {
   R_036800_GC_EA_CPWD_PERFCOUNTER0_SELECT,
   R_036808_GC_EA_CPWD_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_GCEA_CPWD_select1[] = {
   R_036804_GC_EA_CPWD_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx12_GCEA_CPWD = {
   .gpu_block = GCEA_CPWD,
   .name = "GCEA_CPWD",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_GCEA_CPWD_select0,
   .select1 = gfx12_GCEA_CPWD_select1,
   .counter0_lo = R_034980_GC_EA_CPWD_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GCEA_CPWD,
};

/* GCEA_SE */
static unsigned gfx12_GCEA_SE_select0[] = {
   R_036880_GC_EA_SE_PERFCOUNTER0_SELECT,
   R_036888_GC_EA_SE_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_GCEA_SE_select1[] = {
   R_036884_GC_EA_SE_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx12_GCEA_SE = {
   .gpu_block = GCEA_SE,
   .name = "GCEA_SE",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_GCEA_SE_select0,
   .select1 = gfx12_GCEA_SE_select1,
   .counter0_lo = R_0349C0_GC_EA_SE_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GCEA_SE,
};

/* DMA */
static unsigned gfx12_DMA_select0[] = {
   R_037890_SDMA0_PERFCOUNTER0_SELECT,
   R_037898_SDMA0_PERFCOUNTER1_SELECT,
};
static unsigned gfx12_DMA_select1[] = {
   R_037894_SDMA0_PERFCOUNTER0_SELECT1,
   R_03789C_SDMA0_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx12_DMA = {
   .gpu_block = DMA,
   .name = "DMA",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx12_DMA_select0,
   .select1 = gfx12_DMA_select1,
   .counter0_lo = R_035988_SDMA0_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_SDMA,
};

/* MCVML2 */
static unsigned gfx12_MCVML2_select0[] = {
   R_037480_GCVML2_PERFCOUNTER2_0_SELECT,
   R_037484_GCVML2_PERFCOUNTER2_1_SELECT,
};
static unsigned gfx12_MCVML2_select1[] = {
   R_037488_GCVML2_PERFCOUNTER2_0_SELECT1,
   R_03748C_GCVML2_PERFCOUNTER2_1_SELECT1,
};
static unsigned gfx12_MCVML2_cfg[] = {
   R_0374C0_GCMC_VM_L2_PERFCOUNTER0_CFG,
   R_0374C4_GCMC_VM_L2_PERFCOUNTER1_CFG,
   R_0374C8_GCMC_VM_L2_PERFCOUNTER2_CFG,
   R_0374CC_GCMC_VM_L2_PERFCOUNTER3_CFG,
   R_0374D0_GCMC_VM_L2_PERFCOUNTER4_CFG,
   R_0374D4_GCMC_VM_L2_PERFCOUNTER5_CFG,
   R_0374D8_GCMC_VM_L2_PERFCOUNTER6_CFG,
   R_0374DC_GCMC_VM_L2_PERFCOUNTER7_CFG,
};
static struct ac_pc_block_base gfx12_MCVML2 = {
   .gpu_block = MCVML2,
   .name = "MCVML2",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 10,

   .select0 = gfx12_MCVML2_select0,
   .select1 = gfx12_MCVML2_select1,
   .counter0_lo = R_035380_GCVML2_PERFCOUNTER2_0_LO,

   .cfg_cntl = R_0374E0_GCMC_VM_L2_PERFCOUNTER_RSLT_CNTL,
   .cfg_regs = gfx12_MCVML2_cfg,
   .cfg_counter_lo = R_035390_GCMC_VM_L2_PERFCOUNTER_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GPUVMVML2,
};

/* RPB */
static unsigned gfx12_RPB_cfg[] = {
   0x3248,
   0x324C,
   0x3250,
   0x3254,
};
static struct ac_pc_block_base gfx12_RPB = {
   .gpu_block = RPB,
   .name = "RPB",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .cfg_cntl = 0x3258,
   .cfg_regs = gfx12_RPB_cfg,
   .cfg_counter_lo = 0x3264,
};

/* GEDIST */
static unsigned gfx12_GEDIST_select0[] = {
   R_0362B0_GE2_DIST_PERFCOUNTER0_SELECT,
   R_0362B8_GE2_DIST_PERFCOUNTER1_SELECT,
   R_0362C0_GE2_DIST_PERFCOUNTER2_SELECT,
   R_0362C8_GE2_DIST_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GEDIST_select1[] = {
   R_0362B4_GE2_DIST_PERFCOUNTER0_SELECT1,
   R_0362BC_GE2_DIST_PERFCOUNTER1_SELECT1,
   R_0362C4_GE2_DIST_PERFCOUNTER2_SELECT1,
   R_0362CC_GE2_DIST_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GEDIST = {
   .gpu_block = GEDIST,
   .name = "GEDIST",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx12_GEDIST_select0,
   .select1 = gfx12_GEDIST_select1,
   .counter0_lo = R_0342B0_GE2_DIST_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GE2DIST,
};

/* GESE */
static unsigned gfx12_GESE_select0[] = {
   R_0362D0_GE2_SE_PERFCOUNTER0_SELECT,
   R_0362D8_GE2_SE_PERFCOUNTER1_SELECT,
   R_0362E0_GE2_SE_PERFCOUNTER2_SELECT,
   R_0362E8_GE2_SE_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GESE_select1[] = {
   R_0362D4_GE2_SE_PERFCOUNTER0_SELECT1,
   R_0362DC_GE2_SE_PERFCOUNTER1_SELECT1,
   R_0362E4_GE2_SE_PERFCOUNTER2_SELECT1,
   R_0362EC_GE2_SE_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GESE = {
   .gpu_block = GESE,
   .name = "GESE",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 4,

   .select0 = gfx12_GESE_select0,
   .select1 = gfx12_GESE_select1,
   .counter0_lo = R_0342D0_GE2_SE_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GE2SE,
};

/* PC */
static unsigned gfx12_PC_select0[] = {
   R_036640_PC_PERFCOUNTER0_SELECT,
   R_036644_PC_PERFCOUNTER1_SELECT,
   R_036648_PC_PERFCOUNTER2_SELECT,
   R_03664C_PC_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_PC_select1[] = {
   R_036650_PC_PERFCOUNTER0_SELECT1,
   R_036654_PC_PERFCOUNTER1_SELECT1,
   R_036658_PC_PERFCOUNTER2_SELECT1,
   R_03665C_PC_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_PC = {
   .gpu_block = PC,
   .name = "PC",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 4,

   .select0 = gfx12_PC_select0,
   .select1 = gfx12_PC_select1,
   .counter0_lo = R_034630_PC_PERFCOUNTER0_HI, /* Seems inverted */

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_PC,
};

/* GL1XA */
static unsigned gfx12_GL1XA_select0[] = {
   R_037720_GL1XA_PERFCOUNTER0_SELECT,
   R_037728_GL1XA_PERFCOUNTER1_SELECT,
   R_037730_GL1XA_PERFCOUNTER2_SELECT,
   R_037738_GL1XA_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GL1XA_select1[] = {
   R_037724_GL1XA_PERFCOUNTER0_SELECT1,
   R_03772C_GL1XA_PERFCOUNTER1_SELECT1,
   R_037734_GL1XA_PERFCOUNTER2_SELECT1,
   R_03773C_GL1XA_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GL1XA = {
   .gpu_block = GL1XA,
   .name = "GL1XA",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 4,

   .select0 = gfx12_GL1XA_select0,
   .select1 = gfx12_GL1XA_select1,
   .counter0_lo = R_035720_GL1XA_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_GL1XA,
};

/* GL1XC */
static unsigned gfx12_GL1XC_select0[] = {
   R_036EA0_GL1XC_PERFCOUNTER0_SELECT,
   R_036EA8_GL1XC_PERFCOUNTER1_SELECT,
   R_036EB0_GL1XC_PERFCOUNTER2_SELECT,
   R_036EB8_GL1XC_PERFCOUNTER3_SELECT,
};
static unsigned gfx12_GL1XC_select1[] = {
   R_036EA4_GL1XC_PERFCOUNTER0_SELECT1,
   R_036EAC_GL1XC_PERFCOUNTER1_SELECT1,
   R_036EB4_GL1XC_PERFCOUNTER2_SELECT1,
   R_036EBC_GL1XC_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx12_GL1XC = {
   .gpu_block = GL1XC,
   .name = "GL1XC",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 4,

   .select0 = gfx12_GL1XC_select0,
   .select1 = gfx12_GL1XC_select1,
   .counter0_lo = R_034EA0_GL1XC_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_GL1XC,
};

/* WGS */
static unsigned gfx12_WGS_select0[] = {
   0x376CC,
   0x376C0,
};
static unsigned gfx12_WGS_select1[] = {
   0x376C4,
};
static struct ac_pc_block_base gfx12_WGS = {
   .gpu_block = WGS,
   .name = "WGS",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 2,

   .select0 = gfx12_WGS_select0,
   .select1 = gfx12_WGS_select1,
   .counter0_lo = 0x356C8,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_WGS,
};

/* RLCUSER */
static struct ac_pc_block_base gfx12_RLCUSER = {
   .gpu_block = RLC_USER,
   .name = "RLC_USER",
   .distribution = AC_PC_GLOBAL_BLOCK,

   .num_16bit_spm_counters = 0,
   .num_32bit_spm_counters = 1, /* Force to 32-bit */
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_RSPM,
};

static struct ac_pc_block_gfxdescr groups_gfx12[] = {
   {&gfx12_CPF, 43},
   {&gfx12_PA_SU, 828},
   {&gfx12_PA_SC, 821, 2},
   {&gfx12_SPI, 318},
   {&gfx12_SQ, 45},
   {&gfx12_SX, 81},
   {&gfx12_TA, 254},
   {&gfx12_TD, 271},
   {&gfx12_TCP, 99},
   {&gfx12_DB, 441},
   {&gfx12_CB, 315},
   {&gfx12_GRBM, 51},
   {&gfx12_GRBMSE, 25},
   {&gfx12_RLC, 6},
   {&gfx12_DMA, 125},
   {&gfx12_CPG, 95},
   {&gfx12_CPC, 55},
   {&gfx12_MCVML2, 90},
   {&gfx12_RPB, 63},
   {&gfx12_GE, 54},
   {&gfx12_GL1A, 21},
   {&gfx12_GL1C, 121, 4},
   {&gfx12_GL2A, 114, 4},
   {&gfx12_GL2C, 249},
   {&gfx12_CHA, 25},
   {&gfx12_CHC, 94, 4},
   {&gfx12_GCR, 151},
   {&gfx12_UTCL1, 71, 2},
   {&gfx12_GEDIST, 188},
   {&gfx12_GESE, 103},
   {&gfx12_SQ_WGP, 511, 4},
   {&gfx12_PC, 164},
   {&gfx12_GL1XA, 21},
   {&gfx12_GL1XC, 109, 4},
   {&gfx12_WGS, 55},
   {&gfx12_GCEA_CPWD, 32},
   {&gfx12_GCEA_SE, 32},
   {&gfx12_RLCUSER, 0, 4},
};

const struct ac_pc_block_gfxdescr *
ac_gfx12_get_perfcounters(uint32_t *num_blocks)
{
   *num_blocks = ARRAY_SIZE(groups_gfx12);
   return groups_gfx12;
}
