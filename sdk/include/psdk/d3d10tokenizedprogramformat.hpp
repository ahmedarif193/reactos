/*
 * d3d10tokenizedprogramformat.hpp
 *
 * D3D10 Shader Bytecode (DXBC) token format definitions.
 * Clean-room implementation based on the publicly documented
 * D3D10 shader bytecode specification (MSDN).
 *
 * This file is part of the ReactOS PSDK.
 * PROGRAMMER: ReactOS Team
 * LICENSE: LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 */

#ifndef _D3D10TOKENIZEDPROGRAMFORMAT_HPP_
#define _D3D10TOKENIZEDPROGRAMFORMAT_HPP_

/* ---------- Program type ---------- */

typedef enum D3D10_SB_TOKENIZED_PROGRAM_TYPE {
    D3D10_SB_PIXEL_SHADER    = 0,
    D3D10_SB_VERTEX_SHADER   = 1,
    D3D10_SB_GEOMETRY_SHADER = 2,
} D3D10_SB_TOKENIZED_PROGRAM_TYPE;

/* ---------- Opcode type ---------- */

typedef enum D3D10_SB_OPCODE_TYPE {
    D3D10_SB_OPCODE_ADD                              = 0,
    D3D10_SB_OPCODE_AND                              = 1,
    D3D10_SB_OPCODE_BREAK                            = 2,
    D3D10_SB_OPCODE_BREAKC                           = 3,
    D3D10_SB_OPCODE_CALL                             = 4,
    D3D10_SB_OPCODE_CALLC                            = 5,
    D3D10_SB_OPCODE_CASE                             = 6,
    D3D10_SB_OPCODE_CONTINUE                         = 7,
    D3D10_SB_OPCODE_CONTINUEC                        = 8,
    D3D10_SB_OPCODE_CUT                              = 9,
    D3D10_SB_OPCODE_DEFAULT                          = 10,
    D3D10_SB_OPCODE_DERIV_RTX                        = 11,
    D3D10_SB_OPCODE_DERIV_RTY                        = 12,
    D3D10_SB_OPCODE_DISCARD                          = 13,
    D3D10_SB_OPCODE_DIV                              = 14,
    D3D10_SB_OPCODE_DP2                              = 15,
    D3D10_SB_OPCODE_DP3                              = 16,
    D3D10_SB_OPCODE_DP4                              = 17,
    D3D10_SB_OPCODE_ELSE                             = 18,
    D3D10_SB_OPCODE_EMIT                             = 19,
    D3D10_SB_OPCODE_EMITTHENCUT                      = 20,
    D3D10_SB_OPCODE_ENDIF                            = 21,
    D3D10_SB_OPCODE_ENDLOOP                          = 22,
    D3D10_SB_OPCODE_ENDSWITCH                        = 23,
    D3D10_SB_OPCODE_EQ                               = 24,
    D3D10_SB_OPCODE_EXP                              = 25,
    D3D10_SB_OPCODE_FRC                              = 26,
    D3D10_SB_OPCODE_FTOI                             = 27,
    D3D10_SB_OPCODE_FTOU                             = 28,
    D3D10_SB_OPCODE_GE                               = 29,
    D3D10_SB_OPCODE_IADD                             = 30,
    D3D10_SB_OPCODE_IF                               = 31,
    D3D10_SB_OPCODE_IEQ                              = 32,
    D3D10_SB_OPCODE_IGE                              = 33,
    D3D10_SB_OPCODE_ILT                              = 34,
    D3D10_SB_OPCODE_IMAD                             = 35,
    D3D10_SB_OPCODE_IMAX                             = 36,
    D3D10_SB_OPCODE_IMIN                             = 37,
    D3D10_SB_OPCODE_IMUL                             = 38,
    D3D10_SB_OPCODE_INE                              = 39,
    D3D10_SB_OPCODE_INEG                             = 40,
    D3D10_SB_OPCODE_ISHL                             = 41,
    D3D10_SB_OPCODE_ISHR                             = 42,
    D3D10_SB_OPCODE_ITOF                             = 43,
    D3D10_SB_OPCODE_LABEL                            = 44,
    D3D10_SB_OPCODE_LD                               = 45,
    D3D10_SB_OPCODE_LD_MS                            = 46,
    D3D10_SB_OPCODE_LOG                              = 47,
    D3D10_SB_OPCODE_LOOP                             = 48,
    D3D10_SB_OPCODE_LT                               = 49,
    D3D10_SB_OPCODE_MAD                              = 50,
    D3D10_SB_OPCODE_MIN                              = 51,
    D3D10_SB_OPCODE_MAX                              = 52,
    D3D10_SB_OPCODE_CUSTOMDATA                       = 53,
    D3D10_SB_OPCODE_MOV                              = 54,
    D3D10_SB_OPCODE_MOVC                             = 55,
    D3D10_SB_OPCODE_MUL                              = 56,
    D3D10_SB_OPCODE_NE                               = 57,
    D3D10_SB_OPCODE_NOP                              = 58,
    D3D10_SB_OPCODE_NOT                              = 59,
    D3D10_SB_OPCODE_OR                               = 60,
    D3D10_SB_OPCODE_RESINFO                          = 61,
    D3D10_SB_OPCODE_RET                              = 62,
    D3D10_SB_OPCODE_RETC                             = 63,
    D3D10_SB_OPCODE_ROUND_NE                         = 64,
    D3D10_SB_OPCODE_ROUND_NI                         = 65,
    D3D10_SB_OPCODE_ROUND_PI                         = 66,
    D3D10_SB_OPCODE_ROUND_Z                          = 67,
    D3D10_SB_OPCODE_RSQ                              = 68,
    D3D10_SB_OPCODE_SAMPLE                           = 69,
    D3D10_SB_OPCODE_SAMPLE_C                         = 70,
    D3D10_SB_OPCODE_SAMPLE_C_LZ                      = 71,
    D3D10_SB_OPCODE_SAMPLE_L                         = 72,
    D3D10_SB_OPCODE_SAMPLE_D                         = 73,
    D3D10_SB_OPCODE_SAMPLE_B                         = 74,
    D3D10_SB_OPCODE_SQRT                             = 75,
    D3D10_SB_OPCODE_SWITCH                           = 76,
    D3D10_SB_OPCODE_SINCOS                           = 77,
    D3D10_SB_OPCODE_UDIV                             = 78,
    D3D10_SB_OPCODE_ULT                              = 79,
    D3D10_SB_OPCODE_UGE                              = 80,
    D3D10_SB_OPCODE_UMUL                             = 81,
    D3D10_SB_OPCODE_UMAD                             = 82,
    D3D10_SB_OPCODE_UMAX                             = 83,
    D3D10_SB_OPCODE_UMIN                             = 84,
    D3D10_SB_OPCODE_USHR                             = 85,
    D3D10_SB_OPCODE_UTOF                             = 86,
    D3D10_SB_OPCODE_XOR                              = 87,
    D3D10_SB_OPCODE_DCL_RESOURCE                     = 88,
    D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER              = 89,
    D3D10_SB_OPCODE_DCL_SAMPLER                      = 90,
    D3D10_SB_OPCODE_DCL_INDEX_RANGE                  = 91,
    D3D10_SB_OPCODE_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY = 92,
    D3D10_SB_OPCODE_DCL_GS_INPUT_PRIMITIVE           = 93,
    D3D10_SB_OPCODE_DCL_MAX_OUTPUT_VERTEX_COUNT      = 94,
    D3D10_SB_OPCODE_DCL_INPUT                        = 95,
    D3D10_SB_OPCODE_DCL_INPUT_SGV                    = 96,
    D3D10_SB_OPCODE_DCL_INPUT_SIV                    = 97,
    D3D10_SB_OPCODE_DCL_INPUT_PS                     = 98,
    D3D10_SB_OPCODE_DCL_INPUT_PS_SGV                 = 99,
    D3D10_SB_OPCODE_DCL_INPUT_PS_SIV                 = 100,
    D3D10_SB_OPCODE_DCL_OUTPUT                       = 101,
    D3D10_SB_OPCODE_DCL_OUTPUT_SGV                   = 102,
    D3D10_SB_OPCODE_DCL_OUTPUT_SIV                   = 103,
    D3D10_SB_OPCODE_DCL_TEMPS                        = 104,
    D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP               = 105,
    D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS                 = 106,
    D3D10_SB_OPCODE_RESERVED0                        = 107,
    D3D10_1_SB_OPCODE_LOD                            = 108,
    D3D10_1_SB_OPCODE_GATHER4                        = 109,
    D3D10_1_SB_OPCODE_SAMPLE_POS                     = 110,
    D3D10_1_SB_OPCODE_SAMPLE_INFO                    = 111,
    D3D10_SB_NUM_OPCODES, /* = 112 */

    D3D10_SB_OPCODE_EXTENDED  = 0x80000000u,
    D3D10_SB_OPCODE_DOUBLE_EXTENDED = 106, /* alias for reserved slot */
} D3D10_SB_OPCODE_TYPE;

/* ---------- Operand type ---------- */

typedef enum D3D10_SB_OPERAND_TYPE {
    D3D10_SB_OPERAND_TYPE_TEMP                     = 0,
    D3D10_SB_OPERAND_TYPE_INPUT                    = 1,
    D3D10_SB_OPERAND_TYPE_OUTPUT                   = 2,
    D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP           = 3,
    D3D10_SB_OPERAND_TYPE_IMMEDIATE32              = 4,
    D3D10_SB_OPERAND_TYPE_IMMEDIATE64              = 5,
    D3D10_SB_OPERAND_TYPE_SAMPLER                  = 6,
    D3D10_SB_OPERAND_TYPE_RESOURCE                 = 7,
    D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER          = 8,
    D3D10_SB_OPERAND_TYPE_IMMEDIATE_CONSTANT_BUFFER = 9,
    D3D10_SB_OPERAND_TYPE_LABEL                    = 10,
    D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID        = 11,
    D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH             = 12,
    D3D10_SB_OPERAND_TYPE_NULL                     = 13,
    D3D10_SB_OPERAND_TYPE_RASTERIZER               = 14,
    D3D10_SB_OPERAND_TYPE_OUTPUT_COVERAGE_MASK     = 15,
} D3D10_SB_OPERAND_TYPE;

/* ---------- Operand index representation ---------- */

typedef enum D3D10_SB_OPERAND_INDEX_REPRESENTATION {
    D3D10_SB_OPERAND_INDEX_IMMEDIATE32               = 0,
    D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE  = 1,
    D3D10_SB_OPERAND_INDEX_RELATIVE                   = 2,
} D3D10_SB_OPERAND_INDEX_REPRESENTATION;

/* ---------- Operand index dimension ---------- */

typedef enum D3D10_SB_OPERAND_INDEX_DIMENSION {
    D3D10_SB_OPERAND_INDEX_0D = 0,
    D3D10_SB_OPERAND_INDEX_1D = 1,
    D3D10_SB_OPERAND_INDEX_2D = 2,
} D3D10_SB_OPERAND_INDEX_DIMENSION;

/* ---------- Number of components ---------- */

typedef enum D3D10_SB_OPERAND_NUM_COMPONENTS {
    D3D10_SB_OPERAND_0_COMPONENT = 0,
    D3D10_SB_OPERAND_1_COMPONENT = 1,
    D3D10_SB_OPERAND_4_COMPONENT = 2,
} D3D10_SB_OPERAND_NUM_COMPONENTS;

/* ---------- Component name ---------- */

typedef enum D3D10_SB_4_COMPONENT_NAME {
    D3D10_SB_4_COMPONENT_X = 0,
    D3D10_SB_4_COMPONENT_Y = 1,
    D3D10_SB_4_COMPONENT_Z = 2,
    D3D10_SB_4_COMPONENT_W = 3,
} D3D10_SB_4_COMPONENT_NAME;

/* ---------- Component selection mode ---------- */

typedef enum D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE {
    D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE     = 0,
    D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE  = 1,
    D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE = 2,
} D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE;

/* ---------- Operand modifier ---------- */

typedef enum D3D10_SB_OPERAND_MODIFIER {
    D3D10_SB_OPERAND_MODIFIER_NONE   = 0,
    D3D10_SB_OPERAND_MODIFIER_NEG    = 1,
    D3D10_SB_OPERAND_MODIFIER_ABS    = 2,
    D3D10_SB_OPERAND_MODIFIER_ABSNEG = 3,
} D3D10_SB_OPERAND_MODIFIER;

/* ---------- Resource dimension ---------- */

typedef enum D3D10_SB_RESOURCE_DIMENSION {
    D3D10_SB_RESOURCE_DIMENSION_UNKNOWN          = 0,
    D3D10_SB_RESOURCE_DIMENSION_BUFFER           = 1,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D        = 2,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D        = 3,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMS      = 4,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D        = 5,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE      = 6,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY   = 7,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY   = 8,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY = 9,
    D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY = 10,
} D3D10_SB_RESOURCE_DIMENSION;

/* ---------- Resource return type ---------- */

typedef enum D3D10_SB_RESOURCE_RETURN_TYPE {
    D3D10_SB_RETURN_TYPE_UNORM = 1,
    D3D10_SB_RETURN_TYPE_SNORM = 2,
    D3D10_SB_RETURN_TYPE_SINT  = 3,
    D3D10_SB_RETURN_TYPE_UINT  = 4,
    D3D10_SB_RETURN_TYPE_FLOAT = 5,
    D3D10_SB_RETURN_TYPE_MIXED = 6,
} D3D10_SB_RESOURCE_RETURN_TYPE;

/* ---------- Sampler mode ---------- */

typedef enum D3D10_SB_SAMPLER_MODE {
    D3D10_SB_SAMPLER_MODE_DEFAULT    = 0,
    D3D10_SB_SAMPLER_MODE_COMPARISON = 1,
    D3D10_SB_SAMPLER_MODE_MONO       = 2,
} D3D10_SB_SAMPLER_MODE;

/* ---------- Interpolation mode ---------- */

typedef enum D3D10_SB_INTERPOLATION_MODE {
    D3D10_SB_INTERPOLATION_UNDEFINED                     = 0,
    D3D10_SB_INTERPOLATION_CONSTANT                      = 1,
    D3D10_SB_INTERPOLATION_LINEAR                        = 2,
    D3D10_SB_INTERPOLATION_LINEAR_CENTROID               = 3,
    D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE          = 4,
    D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_CENTROID = 5,
    D3D10_SB_INTERPOLATION_LINEAR_SAMPLE                 = 6,
    D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE   = 7,
} D3D10_SB_INTERPOLATION_MODE;

/* ---------- Primitive topology (GS output) ---------- */

typedef enum D3D10_SB_PRIMITIVE_TOPOLOGY {
    D3D10_SB_PRIMITIVE_TOPOLOGY_UNDEFINED     = 0,
    D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST     = 1,
    D3D10_SB_PRIMITIVE_TOPOLOGY_LINELIST      = 2,
    D3D10_SB_PRIMITIVE_TOPOLOGY_LINESTRIP     = 3,
    D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLELIST  = 4,
    D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5,
} D3D10_SB_PRIMITIVE_TOPOLOGY;

/* ---------- Primitive (GS input) ---------- */

typedef enum D3D10_SB_PRIMITIVE {
    D3D10_SB_PRIMITIVE_UNDEFINED    = 0,
    D3D10_SB_PRIMITIVE_POINT        = 1,
    D3D10_SB_PRIMITIVE_LINE         = 2,
    D3D10_SB_PRIMITIVE_TRIANGLE     = 3,
    D3D10_SB_PRIMITIVE_LINE_ADJ     = 6,
    D3D10_SB_PRIMITIVE_TRIANGLE_ADJ = 7,
} D3D10_SB_PRIMITIVE;

/* ---------- Custom data class ---------- */

typedef enum D3D10_SB_CUSTOMDATA_CLASS {
    D3D10_SB_CUSTOMDATA_COMMENT                      = 0,
    D3D10_SB_CUSTOMDATA_DEBUGINFO                    = 1,
    D3D10_SB_CUSTOMDATA_OPAQUE                       = 2,
    D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER = 3,
} D3D10_SB_CUSTOMDATA_CLASS;

/* ---------- Extended opcode type ---------- */

typedef enum D3D10_SB_EXTENDED_OPCODE_TYPE {
    D3D10_SB_EXTENDED_OPCODE_EMPTY           = 0,
    D3D10_SB_EXTENDED_OPCODE_SAMPLE_CONTROLS = 1,
} D3D10_SB_EXTENDED_OPCODE_TYPE;

/* ---------- Extended operand type ---------- */

typedef enum D3D10_SB_EXTENDED_OPERAND_TYPE {
    D3D10_SB_EXTENDED_OPERAND_EMPTY    = 0,
    D3D10_SB_EXTENDED_OPERAND_MODIFIER = 1,
} D3D10_SB_EXTENDED_OPERAND_TYPE;

/* ---------- System value name ---------- */

typedef enum D3D10_SB_NAME {
    D3D10_SB_NAME_UNDEFINED                   = 0,
    D3D10_SB_NAME_POSITION                    = 1,
    D3D10_SB_NAME_CLIP_DISTANCE               = 2,
    D3D10_SB_NAME_CULL_DISTANCE               = 3,
    D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX   = 4,
    D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX        = 5,
    D3D10_SB_NAME_VERTEX_ID                   = 6,
    D3D10_SB_NAME_PRIMITIVE_ID                = 7,
    D3D10_SB_NAME_INSTANCE_ID                 = 8,
    D3D10_SB_NAME_IS_FRONT_FACE              = 9,
    D3D10_SB_NAME_SAMPLE_INDEX               = 10,
} D3D10_SB_NAME;

/* ---------- Instruction test boolean ---------- */

typedef enum D3D10_SB_INSTRUCTION_TEST_BOOLEAN {
    D3D10_SB_INSTRUCTION_TEST_ZERO    = 0,
    D3D10_SB_INSTRUCTION_TEST_NONZERO = 1,
} D3D10_SB_INSTRUCTION_TEST_BOOLEAN;

/* ---------- ResInfo return type ---------- */

typedef enum D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE {
    D3D10_SB_RESINFO_INSTRUCTION_RETURN_FLOAT    = 0,
    D3D10_SB_RESINFO_INSTRUCTION_RETURN_RCPFLOAT = 1,
    D3D10_SB_RESINFO_INSTRUCTION_RETURN_UINT     = 2,
} D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE;

/* ---------- Constant buffer access pattern ---------- */

typedef enum D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN {
    D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED = 0,
    D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED   = 1,
} D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN;

/* ---------- Global flags ---------- */

#define D3D10_SB_GLOBAL_FLAG_REFACTORING_ALLOWED (1u << 11)

/* ---------- Component mask constants ---------- */

#define D3D10_SB_OPERAND_4_COMPONENT_MASK_SHIFT 4
#define D3D10_SB_OPERAND_4_COMPONENT_MASK_X     (1u << 4)
#define D3D10_SB_OPERAND_4_COMPONENT_MASK_Y     (1u << 5)
#define D3D10_SB_OPERAND_4_COMPONENT_MASK_Z     (1u << 6)
#define D3D10_SB_OPERAND_4_COMPONENT_MASK_W     (1u << 7)
#define D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL   \
    (D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y | \
     D3D10_SB_OPERAND_4_COMPONENT_MASK_Z | D3D10_SB_OPERAND_4_COMPONENT_MASK_W)

/* ---------- Operand extended bit ---------- */

#define D3D10_SB_OPERAND_EXTENDED             (1u << 31)

/* ---------- Operand double-extended ---------- */

#define D3D10_SB_OPERAND_DOUBLE_EXTENDED_SHIFT 31
#define D3D10_SB_OPERAND_DOUBLE_EXTENDED_MASK  (1u << 31)

/* ---------- Instruction saturate ---------- */

#define D3D10_SB_INSTRUCTION_SATURATE_ENABLED  (1u << 13)

/* ---------- Immediate address offset bit positions ---------- */

#define D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_U    9
#define D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_V    13
#define D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_W    17
#define D3D10_SB_ADDRESS_OFFSET               4  /* offset nibble width */

/* ================================================================
 *  DECODE macros  --  extract bitfields from 32-bit shader tokens
 * ================================================================ */

/* Program version token (DWORD 0) */
#define DECODE_D3D10_SB_TOKENIZED_PROGRAM_MINOR_VERSION(token) \
    ((unsigned)((token) & 0xFu))

#define DECODE_D3D10_SB_TOKENIZED_PROGRAM_MAJOR_VERSION(token) \
    ((unsigned)(((token) >> 4) & 0xFu))

#define DECODE_D3D10_SB_TOKENIZED_PROGRAM_TYPE(token) \
    ((D3D10_SB_TOKENIZED_PROGRAM_TYPE)(((token) >> 16) & 0xFFFFu))

/* Program length token (DWORD 1) */
#define DECODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(token) \
    ((unsigned)(token))

/* Instruction token */
#define DECODE_D3D10_SB_OPCODE_TYPE(token) \
    ((D3D10_SB_OPCODE_TYPE)((token) & 0x7FFu))

#define DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token) \
    ((unsigned)(((token) >> 24) & 0x7Fu))

#define DECODE_IS_D3D10_SB_OPCODE_EXTENDED(token) \
    ((unsigned)(((token) >> 31) & 0x1u))

#define DECODE_IS_D3D10_SB_INSTRUCTION_SATURATE_ENABLED(token) \
    ((unsigned)((token) & D3D10_SB_INSTRUCTION_SATURATE_ENABLED))

#define DECODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(token) \
    ((D3D10_SB_INSTRUCTION_TEST_BOOLEAN)(((token) >> 18) & 0x1u))

#define DECODE_D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE(token) \
    ((D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE)(((token) >> 11) & 0x3u))

/* Resource declarations */
#define DECODE_D3D10_SB_RESOURCE_DIMENSION(token) \
    ((D3D10_SB_RESOURCE_DIMENSION)(((token) >> 11) & 0x1Fu))

#define DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(token, component) \
    ((D3D10_SB_RESOURCE_RETURN_TYPE)(((token) >> ((component) * 4)) & 0xFu))

/* Sampler declaration */
#define DECODE_D3D10_SB_SAMPLER_MODE(token) \
    ((D3D10_SB_SAMPLER_MODE)(((token) >> 11) & 0xFu))

/* Constant buffer access pattern -- bit 11 */
#define DECODE_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(token) \
    ((D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN)(((token) >> 11) & 0x1u))

/* GS output topology and input primitive */
#define DECODE_D3D10_SB_GS_OUTPUT_PRIMITIVE_TOPOLOGY(token) \
    ((D3D10_SB_PRIMITIVE_TOPOLOGY)(((token) >> 11) & 0x7Fu))

#define DECODE_D3D10_SB_GS_INPUT_PRIMITIVE(token) \
    ((D3D10_SB_PRIMITIVE)(((token) >> 11) & 0x3Fu))

/* Input interpolation mode (PS input declarations) */
#define DECODE_D3D10_SB_INPUT_INTERPOLATION_MODE(token) \
    ((D3D10_SB_INTERPOLATION_MODE)(((token) >> 11) & 0xFu))

/* Global flags */
#define DECODE_D3D10_SB_GLOBAL_FLAGS(token) \
    ((unsigned)(((token) >> 11) & 0x1u))

/* System value name */
#define DECODE_D3D10_SB_NAME(token) \
    ((D3D10_SB_NAME)((token) & 0xFFFFu))

/* Custom data class */
#define DECODE_D3D10_SB_CUSTOMDATA_CLASS(token) \
    ((D3D10_SB_CUSTOMDATA_CLASS)(((token) >> 11) & 0x1FFu))

/* Extended opcode/operand type */
#define DECODE_D3D10_SB_EXTENDED_OPCODE_TYPE(token) \
    ((D3D10_SB_EXTENDED_OPCODE_TYPE)((token) & 0x3Fu))

#define DECODE_D3D10_SB_EXTENDED_OPERAND_TYPE(token) \
    ((D3D10_SB_EXTENDED_OPERAND_TYPE)((token) & 0x3Fu))

/* Operand modifier in extended operand token */
#define DECODE_D3D10_SB_OPERAND_MODIFIER(token) \
    ((D3D10_SB_OPERAND_MODIFIER)(((token) >> 6) & 0x3u))

/* Operand token decoding */
#define DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(token) \
    ((D3D10_SB_OPERAND_NUM_COMPONENTS)((token) & 0x3u))

#define DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(token) \
    ((D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE)(((token) >> 2) & 0x3u))

#define DECODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(token) \
    ((unsigned)((token) & 0xF0u))

#define DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, comp) \
    ((D3D10_SB_4_COMPONENT_NAME)(((token) >> (4 + 2 * (comp))) & 0x3u))

#define DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(token) \
    ((D3D10_SB_4_COMPONENT_NAME)(((token) >> 4) & 0x3u))

#define DECODE_D3D10_SB_OPERAND_TYPE(token) \
    ((D3D10_SB_OPERAND_TYPE)(((token) >> 12) & 0xFFu))

#define DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(token) \
    ((D3D10_SB_OPERAND_INDEX_DIMENSION)(((token) >> 20) & 0x3u))

#define DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(dim, token) \
    ((D3D10_SB_OPERAND_INDEX_REPRESENTATION)(((token) >> (22 + 3 * (dim))) & 0x7u))

#define DECODE_IS_D3D10_SB_OPERAND_EXTENDED(token) \
    ((unsigned)(((token) >> 31) & 0x1u))

#define DECODE_IS_D3D10_SB_OPERAND_DOUBLE_EXTENDED(token) \
    ((unsigned)(((token) & D3D10_SB_OPERAND_DOUBLE_EXTENDED_MASK) >> D3D10_SB_OPERAND_DOUBLE_EXTENDED_SHIFT))

/* Immediate texel address offset (sign-extended 4-bit nibble) */
#define DECODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(offset_pos, token) \
    ((int)(((token) >> (offset_pos)) & 0xFu) | \
     ((((token) >> (offset_pos)) & 0x8u) ? (int)0xFFFFFFF0u : 0))

/* ================================================================
 *  Type aliases used by Mesa d3d10umd frontend
 * ================================================================ */

/* These aliases allow code to use the shorter GS_INPUT_PRIMITIVE etc. as types */
typedef D3D10_SB_PRIMITIVE          D3D10_SB_GS_INPUT_PRIMITIVE;
typedef D3D10_SB_PRIMITIVE_TOPOLOGY D3D10_SB_GS_OUTPUT_PRIMITIVE_TOPOLOGY;
typedef D3D10_SB_INTERPOLATION_MODE D3D10_SB_INPUT_INTERPOLATION_MODE;

#endif /* _D3D10TOKENIZEDPROGRAMFORMAT_HPP_ */
