/*
 * PROJECT:     ReactOS Direct3D 11 Header
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3D11 interface declarations matching Windows SDK
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#ifndef __d3d11_h__
#define __d3d11_h__

#include "oaidl.h"
#include "ocidl.h"
#include "dxgi.h"
#include "d3dcommon.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef interface ID3D11DeviceChild ID3D11DeviceChild;
typedef interface ID3D11DepthStencilState ID3D11DepthStencilState;
typedef interface ID3D11BlendState ID3D11BlendState;
typedef interface ID3D11RasterizerState ID3D11RasterizerState;
typedef interface ID3D11Resource ID3D11Resource;
typedef interface ID3D11Buffer ID3D11Buffer;
typedef interface ID3D11Texture1D ID3D11Texture1D;
typedef interface ID3D11Texture2D ID3D11Texture2D;
typedef interface ID3D11Texture3D ID3D11Texture3D;
typedef interface ID3D11View ID3D11View;
typedef interface ID3D11ShaderResourceView ID3D11ShaderResourceView;
typedef interface ID3D11RenderTargetView ID3D11RenderTargetView;
typedef interface ID3D11DepthStencilView ID3D11DepthStencilView;
typedef interface ID3D11UnorderedAccessView ID3D11UnorderedAccessView;
typedef interface ID3D11VertexShader ID3D11VertexShader;
typedef interface ID3D11HullShader ID3D11HullShader;
typedef interface ID3D11DomainShader ID3D11DomainShader;
typedef interface ID3D11GeometryShader ID3D11GeometryShader;
typedef interface ID3D11PixelShader ID3D11PixelShader;
typedef interface ID3D11ComputeShader ID3D11ComputeShader;
typedef interface ID3D11InputLayout ID3D11InputLayout;
typedef interface ID3D11SamplerState ID3D11SamplerState;
typedef interface ID3D11Asynchronous ID3D11Asynchronous;
typedef interface ID3D11Query ID3D11Query;
/* ID3D11Predicate is identical to ID3D11Query - defined as typedef later */
typedef interface ID3D11Counter ID3D11Counter;
typedef interface ID3D11ClassInstance ID3D11ClassInstance;
typedef interface ID3D11ClassLinkage ID3D11ClassLinkage;
typedef interface ID3D11CommandList ID3D11CommandList;
typedef interface ID3D11DeviceContext ID3D11DeviceContext;
typedef interface ID3D11Device ID3D11Device;

/* ========================================================================= */
/*                             Constants                                     */
/* ========================================================================= */

#define D3D11_SDK_VERSION (7)

#define D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT  8
#define D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT   16
#define D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT   128
#define D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT   14
#define D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT   32
#define D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT 32
#define D3D11_SO_BUFFER_SLOT_COUNT  4
#define D3D11_SO_STREAM_COUNT   4
#define D3D11_SO_OUTPUT_COMPONENT_COUNT 128
#define D3D11_PS_CS_UAV_REGISTER_COUNT  8
#define D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE 16
#define D3D11_DEFAULT_SAMPLE_MASK  (0xffffffff)
#define D3D11_DEFAULT_STENCIL_READ_MASK  (0xff)
#define D3D11_DEFAULT_STENCIL_WRITE_MASK  (0xff)
#define D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL (0xffffffff)
#define D3D11_KEEP_UNORDERED_ACCESS_VIEWS (0xffffffff)
#define D3D11_APPEND_ALIGNED_ELEMENT (0xffffffff)
#define D3D11_DEFAULT_DEPTH_BIAS (0)
#define D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION 16384
#define D3D11_REQ_TEXTURE1D_U_DIMENSION 16384
#define D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION 2048

#define D3D11_FLOAT32_MAX (3.402823466e+38f)

/* ========================================================================= */
/*                             Enumerations                                  */
/* ========================================================================= */

typedef enum D3D11_INPUT_CLASSIFICATION
{
    D3D11_INPUT_PER_VERTEX_DATA = 0,
    D3D11_INPUT_PER_INSTANCE_DATA = 1,
} D3D11_INPUT_CLASSIFICATION;

typedef enum D3D11_FILL_MODE
{
    D3D11_FILL_WIREFRAME = 2,
    D3D11_FILL_SOLID = 3,
} D3D11_FILL_MODE;

typedef D3D_PRIMITIVE_TOPOLOGY D3D11_PRIMITIVE_TOPOLOGY;
typedef D3D_PRIMITIVE D3D11_PRIMITIVE;

typedef enum D3D11_CULL_MODE
{
    D3D11_CULL_NONE = 1,
    D3D11_CULL_FRONT = 2,
    D3D11_CULL_BACK = 3,
} D3D11_CULL_MODE;

typedef enum D3D11_RESOURCE_DIMENSION
{
    D3D11_RESOURCE_DIMENSION_UNKNOWN = 0,
    D3D11_RESOURCE_DIMENSION_BUFFER = 1,
    D3D11_RESOURCE_DIMENSION_TEXTURE1D = 2,
    D3D11_RESOURCE_DIMENSION_TEXTURE2D = 3,
    D3D11_RESOURCE_DIMENSION_TEXTURE3D = 4,
} D3D11_RESOURCE_DIMENSION;

typedef D3D_SRV_DIMENSION D3D11_SRV_DIMENSION;

typedef enum D3D11_DSV_DIMENSION
{
    D3D11_DSV_DIMENSION_UNKNOWN = 0,
    D3D11_DSV_DIMENSION_TEXTURE1D = 1,
    D3D11_DSV_DIMENSION_TEXTURE1DARRAY = 2,
    D3D11_DSV_DIMENSION_TEXTURE2D = 3,
    D3D11_DSV_DIMENSION_TEXTURE2DARRAY = 4,
    D3D11_DSV_DIMENSION_TEXTURE2DMS = 5,
    D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY = 6,
} D3D11_DSV_DIMENSION;

typedef enum D3D11_RTV_DIMENSION
{
    D3D11_RTV_DIMENSION_UNKNOWN = 0,
    D3D11_RTV_DIMENSION_BUFFER = 1,
    D3D11_RTV_DIMENSION_TEXTURE1D = 2,
    D3D11_RTV_DIMENSION_TEXTURE1DARRAY = 3,
    D3D11_RTV_DIMENSION_TEXTURE2D = 4,
    D3D11_RTV_DIMENSION_TEXTURE2DARRAY = 5,
    D3D11_RTV_DIMENSION_TEXTURE2DMS = 6,
    D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY = 7,
    D3D11_RTV_DIMENSION_TEXTURE3D = 8,
} D3D11_RTV_DIMENSION;

typedef enum D3D11_UAV_DIMENSION
{
    D3D11_UAV_DIMENSION_UNKNOWN = 0,
    D3D11_UAV_DIMENSION_BUFFER = 1,
    D3D11_UAV_DIMENSION_TEXTURE1D = 2,
    D3D11_UAV_DIMENSION_TEXTURE1DARRAY = 3,
    D3D11_UAV_DIMENSION_TEXTURE2D = 4,
    D3D11_UAV_DIMENSION_TEXTURE2DARRAY = 5,
    D3D11_UAV_DIMENSION_TEXTURE3D = 8,
} D3D11_UAV_DIMENSION;

typedef enum D3D11_USAGE
{
    D3D11_USAGE_DEFAULT = 0,
    D3D11_USAGE_IMMUTABLE = 1,
    D3D11_USAGE_DYNAMIC = 2,
    D3D11_USAGE_STAGING = 3,
} D3D11_USAGE;

typedef enum D3D11_BIND_FLAG
{
    D3D11_BIND_VERTEX_BUFFER = 0x1L,
    D3D11_BIND_INDEX_BUFFER = 0x2L,
    D3D11_BIND_CONSTANT_BUFFER = 0x4L,
    D3D11_BIND_SHADER_RESOURCE = 0x8L,
    D3D11_BIND_STREAM_OUTPUT = 0x10L,
    D3D11_BIND_RENDER_TARGET = 0x20L,
    D3D11_BIND_DEPTH_STENCIL = 0x40L,
    D3D11_BIND_UNORDERED_ACCESS = 0x80L,
    D3D11_BIND_DECODER = 0x200L,
    D3D11_BIND_VIDEO_ENCODER = 0x400L,
} D3D11_BIND_FLAG;

typedef enum D3D11_CPU_ACCESS_FLAG
{
    D3D11_CPU_ACCESS_WRITE = 0x10000L,
    D3D11_CPU_ACCESS_READ = 0x20000L,
} D3D11_CPU_ACCESS_FLAG;

typedef enum D3D11_RESOURCE_MISC_FLAG
{
    D3D11_RESOURCE_MISC_GENERATE_MIPS = 0x1L,
    D3D11_RESOURCE_MISC_SHARED = 0x2L,
    D3D11_RESOURCE_MISC_TEXTURECUBE = 0x4L,
    D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS = 0x10L,
    D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS = 0x20L,
    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED = 0x40L,
    D3D11_RESOURCE_MISC_RESOURCE_CLAMP = 0x80L,
    D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX = 0x100L,
    D3D11_RESOURCE_MISC_GDI_COMPATIBLE = 0x200L,
} D3D11_RESOURCE_MISC_FLAG;

typedef enum D3D11_MAP
{
    D3D11_MAP_READ = 1,
    D3D11_MAP_WRITE = 2,
    D3D11_MAP_READ_WRITE = 3,
    D3D11_MAP_WRITE_DISCARD = 4,
    D3D11_MAP_WRITE_NO_OVERWRITE = 5,
} D3D11_MAP;

typedef enum D3D11_MAP_FLAG
{
    D3D11_MAP_FLAG_DO_NOT_WAIT = 0x100000L,
} D3D11_MAP_FLAG;

typedef enum D3D11_CLEAR_FLAG
{
    D3D11_CLEAR_DEPTH = 0x1L,
    D3D11_CLEAR_STENCIL = 0x2L,
} D3D11_CLEAR_FLAG;

typedef enum D3D11_COMPARISON_FUNC
{
    D3D11_COMPARISON_NEVER = 1,
    D3D11_COMPARISON_LESS = 2,
    D3D11_COMPARISON_EQUAL = 3,
    D3D11_COMPARISON_LESS_EQUAL = 4,
    D3D11_COMPARISON_GREATER = 5,
    D3D11_COMPARISON_NOT_EQUAL = 6,
    D3D11_COMPARISON_GREATER_EQUAL = 7,
    D3D11_COMPARISON_ALWAYS = 8,
} D3D11_COMPARISON_FUNC;

typedef enum D3D11_DEPTH_WRITE_MASK
{
    D3D11_DEPTH_WRITE_MASK_ZERO = 0,
    D3D11_DEPTH_WRITE_MASK_ALL = 1,
} D3D11_DEPTH_WRITE_MASK;

typedef enum D3D11_STENCIL_OP
{
    D3D11_STENCIL_OP_KEEP = 1,
    D3D11_STENCIL_OP_ZERO = 2,
    D3D11_STENCIL_OP_REPLACE = 3,
    D3D11_STENCIL_OP_INCR_SAT = 4,
    D3D11_STENCIL_OP_DECR_SAT = 5,
    D3D11_STENCIL_OP_INVERT = 6,
    D3D11_STENCIL_OP_INCR = 7,
    D3D11_STENCIL_OP_DECR = 8,
} D3D11_STENCIL_OP;

typedef enum D3D11_BLEND
{
    D3D11_BLEND_ZERO = 1,
    D3D11_BLEND_ONE = 2,
    D3D11_BLEND_SRC_COLOR = 3,
    D3D11_BLEND_INV_SRC_COLOR = 4,
    D3D11_BLEND_SRC_ALPHA = 5,
    D3D11_BLEND_INV_SRC_ALPHA = 6,
    D3D11_BLEND_DEST_ALPHA = 7,
    D3D11_BLEND_INV_DEST_ALPHA = 8,
    D3D11_BLEND_DEST_COLOR = 9,
    D3D11_BLEND_INV_DEST_COLOR = 10,
    D3D11_BLEND_SRC_ALPHA_SAT = 11,
    D3D11_BLEND_BLEND_FACTOR = 14,
    D3D11_BLEND_INV_BLEND_FACTOR = 15,
    D3D11_BLEND_SRC1_COLOR = 16,
    D3D11_BLEND_INV_SRC1_COLOR = 17,
    D3D11_BLEND_SRC1_ALPHA = 18,
    D3D11_BLEND_INV_SRC1_ALPHA = 19,
} D3D11_BLEND;

typedef enum D3D11_BLEND_OP
{
    D3D11_BLEND_OP_ADD = 1,
    D3D11_BLEND_OP_SUBTRACT = 2,
    D3D11_BLEND_OP_REV_SUBTRACT = 3,
    D3D11_BLEND_OP_MIN = 4,
    D3D11_BLEND_OP_MAX = 5,
} D3D11_BLEND_OP;

typedef enum D3D11_COLOR_WRITE_ENABLE
{
    D3D11_COLOR_WRITE_ENABLE_RED = 1,
    D3D11_COLOR_WRITE_ENABLE_GREEN = 2,
    D3D11_COLOR_WRITE_ENABLE_BLUE = 4,
    D3D11_COLOR_WRITE_ENABLE_ALPHA = 8,
    D3D11_COLOR_WRITE_ENABLE_ALL = (1 | 2 | 4 | 8),
} D3D11_COLOR_WRITE_ENABLE;

typedef enum D3D11_FILTER
{
    D3D11_FILTER_MIN_MAG_MIP_POINT = 0,
    D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR = 0x1,
    D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x4,
    D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR = 0x5,
    D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT = 0x10,
    D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x11,
    D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT = 0x14,
    D3D11_FILTER_MIN_MAG_MIP_LINEAR = 0x15,
    D3D11_FILTER_ANISOTROPIC = 0x55,
    D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT = 0x80,
    D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR = 0x81,
    D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x84,
    D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR = 0x85,
    D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT = 0x90,
    D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x91,
    D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT = 0x94,
    D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR = 0x95,
    D3D11_FILTER_COMPARISON_ANISOTROPIC = 0xd5,
} D3D11_FILTER;

typedef enum D3D11_TEXTURE_ADDRESS_MODE
{
    D3D11_TEXTURE_ADDRESS_WRAP = 1,
    D3D11_TEXTURE_ADDRESS_MIRROR = 2,
    D3D11_TEXTURE_ADDRESS_CLAMP = 3,
    D3D11_TEXTURE_ADDRESS_BORDER = 4,
    D3D11_TEXTURE_ADDRESS_MIRROR_ONCE = 5,
} D3D11_TEXTURE_ADDRESS_MODE;

typedef enum D3D11_QUERY
{
    D3D11_QUERY_EVENT = 0,
    D3D11_QUERY_OCCLUSION = 1,
    D3D11_QUERY_TIMESTAMP = 2,
    D3D11_QUERY_TIMESTAMP_DISJOINT = 3,
    D3D11_QUERY_PIPELINE_STATISTICS = 4,
    D3D11_QUERY_OCCLUSION_PREDICATE = 5,
    D3D11_QUERY_SO_STATISTICS = 6,
    D3D11_QUERY_SO_OVERFLOW_PREDICATE = 7,
    D3D11_QUERY_SO_STATISTICS_STREAM0 = 8,
    D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0 = 9,
    D3D11_QUERY_SO_STATISTICS_STREAM1 = 10,
    D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1 = 11,
    D3D11_QUERY_SO_STATISTICS_STREAM2 = 12,
    D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2 = 13,
    D3D11_QUERY_SO_STATISTICS_STREAM3 = 14,
    D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3 = 15,
} D3D11_QUERY;

typedef enum D3D11_COUNTER
{
    D3D11_COUNTER_DEVICE_DEPENDENT_0 = 0x40000000,
} D3D11_COUNTER;

typedef enum D3D11_COUNTER_TYPE
{
    D3D11_COUNTER_TYPE_FLOAT32 = 0,
    D3D11_COUNTER_TYPE_UINT16 = 1,
    D3D11_COUNTER_TYPE_UINT32 = 2,
    D3D11_COUNTER_TYPE_UINT64 = 3,
} D3D11_COUNTER_TYPE;

typedef enum D3D11_DEVICE_CONTEXT_TYPE
{
    D3D11_DEVICE_CONTEXT_IMMEDIATE = 0,
    D3D11_DEVICE_CONTEXT_DEFERRED = 1,
} D3D11_DEVICE_CONTEXT_TYPE;

typedef enum D3D11_FEATURE
{
    D3D11_FEATURE_THREADING = 0,
    D3D11_FEATURE_DOUBLES = 1,
    D3D11_FEATURE_FORMAT_SUPPORT = 2,
    D3D11_FEATURE_FORMAT_SUPPORT2 = 3,
    D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS = 4,
    D3D11_FEATURE_D3D11_OPTIONS = 5,
    D3D11_FEATURE_ARCHITECTURE_INFO = 6,
    D3D11_FEATURE_D3D9_OPTIONS = 7,
    D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT = 8,
    D3D11_FEATURE_D3D9_SHADOW_SUPPORT = 9,
} D3D11_FEATURE;

typedef enum D3D11_CREATE_DEVICE_FLAG
{
    D3D11_CREATE_DEVICE_SINGLETHREADED = 0x1,
    D3D11_CREATE_DEVICE_DEBUG = 0x2,
    D3D11_CREATE_DEVICE_SWITCH_TO_REF = 0x4,
    D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS = 0x8,
    D3D11_CREATE_DEVICE_BGRA_SUPPORT = 0x20,
    D3D11_CREATE_DEVICE_DEBUGGABLE = 0x40,
    D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY = 0x80,
    D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT = 0x100,
    D3D11_CREATE_DEVICE_VIDEO_SUPPORT = 0x800,
} D3D11_CREATE_DEVICE_FLAG;

typedef enum D3D11_BUFFEREX_SRV_FLAG
{
    D3D11_BUFFEREX_SRV_FLAG_RAW = 0x1,
} D3D11_BUFFEREX_SRV_FLAG;

typedef enum D3D11_DSV_FLAG
{
    D3D11_DSV_READ_ONLY_DEPTH = 0x1L,
    D3D11_DSV_READ_ONLY_STENCIL = 0x2L,
} D3D11_DSV_FLAG;

typedef enum D3D11_BUFFER_UAV_FLAG
{
    D3D11_BUFFER_UAV_FLAG_RAW = 0x1,
    D3D11_BUFFER_UAV_FLAG_APPEND = 0x2,
    D3D11_BUFFER_UAV_FLAG_COUNTER = 0x4,
} D3D11_BUFFER_UAV_FLAG;

typedef enum D3D11_RAISE_FLAG
{
    D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR = 0x1L,
} D3D11_RAISE_FLAG;

typedef enum D3D11_FORMAT_SUPPORT
{
    D3D11_FORMAT_SUPPORT_BUFFER = 0x1,
    D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER = 0x2,
    D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER = 0x4,
    D3D11_FORMAT_SUPPORT_SO_BUFFER = 0x8,
    D3D11_FORMAT_SUPPORT_TEXTURE1D = 0x10,
    D3D11_FORMAT_SUPPORT_TEXTURE2D = 0x20,
    D3D11_FORMAT_SUPPORT_TEXTURE3D = 0x40,
    D3D11_FORMAT_SUPPORT_TEXTURECUBE = 0x80,
    D3D11_FORMAT_SUPPORT_SHADER_LOAD = 0x100,
    D3D11_FORMAT_SUPPORT_SHADER_SAMPLE = 0x200,
    D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON = 0x400,
    D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_MONO_TEXT = 0x800,
    D3D11_FORMAT_SUPPORT_MIP = 0x1000,
    D3D11_FORMAT_SUPPORT_MIP_AUTOGEN = 0x2000,
    D3D11_FORMAT_SUPPORT_RENDER_TARGET = 0x4000,
    D3D11_FORMAT_SUPPORT_BLENDABLE = 0x8000,
    D3D11_FORMAT_SUPPORT_DEPTH_STENCIL = 0x10000,
    D3D11_FORMAT_SUPPORT_CPU_LOCKABLE = 0x20000,
    D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE = 0x40000,
    D3D11_FORMAT_SUPPORT_DISPLAY = 0x80000,
    D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT = 0x100000,
    D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET = 0x200000,
    D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD = 0x400000,
    D3D11_FORMAT_SUPPORT_SHADER_GATHER = 0x800000,
    D3D11_FORMAT_SUPPORT_BACK_BUFFER_CAST = 0x1000000,
    D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW = 0x2000000,
    D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON = 0x4000000,
} D3D11_FORMAT_SUPPORT;

typedef enum D3D11_ASYNC_GETDATA_FLAG
{
    D3D11_ASYNC_GETDATA_DONOTFLUSH = 0x1,
} D3D11_ASYNC_GETDATA_FLAG;

typedef enum D3D11_QUERY_MISC_FLAG
{
    D3D11_QUERY_MISC_PREDICATEHINT = 0x1,
} D3D11_QUERY_MISC_FLAG;

typedef RECT D3D11_RECT;

/* ========================================================================= */
/*                             Structures                                    */
/* ========================================================================= */

typedef struct D3D11_INPUT_ELEMENT_DESC
{
    LPCSTR SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    D3D11_INPUT_CLASSIFICATION InputSlotClass;
    UINT InstanceDataStepRate;
} D3D11_INPUT_ELEMENT_DESC;

typedef struct D3D11_SO_DECLARATION_ENTRY
{
    UINT Stream;
    LPCSTR SemanticName;
    UINT SemanticIndex;
    BYTE StartComponent;
    BYTE ComponentCount;
    BYTE OutputSlot;
} D3D11_SO_DECLARATION_ENTRY;

typedef struct D3D11_VIEWPORT
{
    FLOAT TopLeftX;
    FLOAT TopLeftY;
    FLOAT Width;
    FLOAT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
} D3D11_VIEWPORT;

typedef struct D3D11_BOX
{
    UINT left;
    UINT top;
    UINT front;
    UINT right;
    UINT bottom;
    UINT back;
} D3D11_BOX;

typedef struct D3D11_DEPTH_STENCILOP_DESC
{
    D3D11_STENCIL_OP StencilFailOp;
    D3D11_STENCIL_OP StencilDepthFailOp;
    D3D11_STENCIL_OP StencilPassOp;
    D3D11_COMPARISON_FUNC StencilFunc;
} D3D11_DEPTH_STENCILOP_DESC;

typedef struct D3D11_DEPTH_STENCIL_DESC
{
    BOOL DepthEnable;
    D3D11_DEPTH_WRITE_MASK DepthWriteMask;
    D3D11_COMPARISON_FUNC DepthFunc;
    BOOL StencilEnable;
    UINT8 StencilReadMask;
    UINT8 StencilWriteMask;
    D3D11_DEPTH_STENCILOP_DESC FrontFace;
    D3D11_DEPTH_STENCILOP_DESC BackFace;
} D3D11_DEPTH_STENCIL_DESC;

typedef struct D3D11_RENDER_TARGET_BLEND_DESC
{
    BOOL BlendEnable;
    D3D11_BLEND SrcBlend;
    D3D11_BLEND DestBlend;
    D3D11_BLEND_OP BlendOp;
    D3D11_BLEND SrcBlendAlpha;
    D3D11_BLEND DestBlendAlpha;
    D3D11_BLEND_OP BlendOpAlpha;
    UINT8 RenderTargetWriteMask;
} D3D11_RENDER_TARGET_BLEND_DESC;

typedef struct D3D11_BLEND_DESC
{
    BOOL AlphaToCoverageEnable;
    BOOL IndependentBlendEnable;
    D3D11_RENDER_TARGET_BLEND_DESC RenderTarget[8];
} D3D11_BLEND_DESC;

typedef struct D3D11_RASTERIZER_DESC
{
    D3D11_FILL_MODE FillMode;
    D3D11_CULL_MODE CullMode;
    BOOL FrontCounterClockwise;
    INT DepthBias;
    FLOAT DepthBiasClamp;
    FLOAT SlopeScaledDepthBias;
    BOOL DepthClipEnable;
    BOOL ScissorEnable;
    BOOL MultisampleEnable;
    BOOL AntialiasedLineEnable;
} D3D11_RASTERIZER_DESC;

typedef struct D3D11_SUBRESOURCE_DATA
{
    const void *pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
} D3D11_SUBRESOURCE_DATA;

typedef struct D3D11_MAPPED_SUBRESOURCE
{
    void *pData;
    UINT RowPitch;
    UINT DepthPitch;
} D3D11_MAPPED_SUBRESOURCE;

typedef struct D3D11_BUFFER_DESC
{
    UINT ByteWidth;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
} D3D11_BUFFER_DESC;

typedef struct D3D11_TEXTURE1D_DESC
{
    UINT Width;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D11_TEXTURE1D_DESC;

typedef struct D3D11_TEXTURE2D_DESC
{
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D11_TEXTURE2D_DESC;

typedef struct D3D11_TEXTURE3D_DESC
{
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    DXGI_FORMAT Format;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D11_TEXTURE3D_DESC;

/* Shader resource view sub-structs */
typedef struct D3D11_BUFFER_SRV { UINT FirstElement; UINT NumElements; } D3D11_BUFFER_SRV;
typedef struct D3D11_BUFFEREX_SRV { UINT FirstElement; UINT NumElements; UINT Flags; } D3D11_BUFFEREX_SRV;
typedef struct D3D11_TEX1D_SRV { UINT MostDetailedMip; UINT MipLevels; } D3D11_TEX1D_SRV;
typedef struct D3D11_TEX1D_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX1D_ARRAY_SRV;
typedef struct D3D11_TEX2D_SRV { UINT MostDetailedMip; UINT MipLevels; } D3D11_TEX2D_SRV;
typedef struct D3D11_TEX2D_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX2D_ARRAY_SRV;
typedef struct D3D11_TEX3D_SRV { UINT MostDetailedMip; UINT MipLevels; } D3D11_TEX3D_SRV;
typedef struct D3D11_TEXCUBE_SRV { UINT MostDetailedMip; UINT MipLevels; } D3D11_TEXCUBE_SRV;
typedef struct D3D11_TEXCUBE_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT First2DArrayFace; UINT NumCubes; } D3D11_TEXCUBE_ARRAY_SRV;
typedef struct D3D11_TEX2DMS_SRV { UINT UnusedField_NothingToDefine; } D3D11_TEX2DMS_SRV;
typedef struct D3D11_TEX2DMS_ARRAY_SRV { UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX2DMS_ARRAY_SRV;

typedef struct D3D11_SHADER_RESOURCE_VIEW_DESC
{
    DXGI_FORMAT Format;
    D3D11_SRV_DIMENSION ViewDimension;
    union
    {
        D3D11_BUFFER_SRV Buffer;
        D3D11_TEX1D_SRV Texture1D;
        D3D11_TEX1D_ARRAY_SRV Texture1DArray;
        D3D11_TEX2D_SRV Texture2D;
        D3D11_TEX2D_ARRAY_SRV Texture2DArray;
        D3D11_TEX2DMS_SRV Texture2DMS;
        D3D11_TEX2DMS_ARRAY_SRV Texture2DMSArray;
        D3D11_TEX3D_SRV Texture3D;
        D3D11_TEXCUBE_SRV TextureCube;
        D3D11_TEXCUBE_ARRAY_SRV TextureCubeArray;
        D3D11_BUFFEREX_SRV BufferEx;
    };
} D3D11_SHADER_RESOURCE_VIEW_DESC;

/* Render target view sub-structs */
typedef struct D3D11_BUFFER_RTV { union { UINT FirstElement; UINT ElementOffset; }; union { UINT NumElements; UINT ElementWidth; }; } D3D11_BUFFER_RTV;
typedef struct D3D11_TEX1D_RTV { UINT MipSlice; } D3D11_TEX1D_RTV;
typedef struct D3D11_TEX1D_ARRAY_RTV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX1D_ARRAY_RTV;
typedef struct D3D11_TEX2D_RTV { UINT MipSlice; } D3D11_TEX2D_RTV;
typedef struct D3D11_TEX2DMS_RTV { UINT UnusedField_NothingToDefine; } D3D11_TEX2DMS_RTV;
typedef struct D3D11_TEX2D_ARRAY_RTV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX2D_ARRAY_RTV;
typedef struct D3D11_TEX2DMS_ARRAY_RTV { UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX2DMS_ARRAY_RTV;
typedef struct D3D11_TEX3D_RTV { UINT MipSlice; UINT FirstWSlice; UINT WSize; } D3D11_TEX3D_RTV;

typedef struct D3D11_RENDER_TARGET_VIEW_DESC
{
    DXGI_FORMAT Format;
    D3D11_RTV_DIMENSION ViewDimension;
    union
    {
        D3D11_BUFFER_RTV Buffer;
        D3D11_TEX1D_RTV Texture1D;
        D3D11_TEX1D_ARRAY_RTV Texture1DArray;
        D3D11_TEX2D_RTV Texture2D;
        D3D11_TEX2D_ARRAY_RTV Texture2DArray;
        D3D11_TEX2DMS_RTV Texture2DMS;
        D3D11_TEX2DMS_ARRAY_RTV Texture2DMSArray;
        D3D11_TEX3D_RTV Texture3D;
    };
} D3D11_RENDER_TARGET_VIEW_DESC;

/* Depth stencil view sub-structs */
typedef struct D3D11_TEX1D_DSV { UINT MipSlice; } D3D11_TEX1D_DSV;
typedef struct D3D11_TEX1D_ARRAY_DSV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX1D_ARRAY_DSV;
typedef struct D3D11_TEX2D_DSV { UINT MipSlice; } D3D11_TEX2D_DSV;
typedef struct D3D11_TEX2D_ARRAY_DSV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX2D_ARRAY_DSV;
typedef struct D3D11_TEX2DMS_DSV { UINT UnusedField_NothingToDefine; } D3D11_TEX2DMS_DSV;
typedef struct D3D11_TEX2DMS_ARRAY_DSV { UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX2DMS_ARRAY_DSV;

typedef struct D3D11_DEPTH_STENCIL_VIEW_DESC
{
    DXGI_FORMAT Format;
    D3D11_DSV_DIMENSION ViewDimension;
    UINT Flags;
    union
    {
        D3D11_TEX1D_DSV Texture1D;
        D3D11_TEX1D_ARRAY_DSV Texture1DArray;
        D3D11_TEX2D_DSV Texture2D;
        D3D11_TEX2D_ARRAY_DSV Texture2DArray;
        D3D11_TEX2DMS_DSV Texture2DMS;
        D3D11_TEX2DMS_ARRAY_DSV Texture2DMSArray;
    };
} D3D11_DEPTH_STENCIL_VIEW_DESC;

/* Unordered access view sub-structs */
typedef struct D3D11_BUFFER_UAV { UINT FirstElement; UINT NumElements; UINT Flags; } D3D11_BUFFER_UAV;
typedef struct D3D11_TEX1D_UAV { UINT MipSlice; } D3D11_TEX1D_UAV;
typedef struct D3D11_TEX1D_ARRAY_UAV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX1D_ARRAY_UAV;
typedef struct D3D11_TEX2D_UAV { UINT MipSlice; } D3D11_TEX2D_UAV;
typedef struct D3D11_TEX2D_ARRAY_UAV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } D3D11_TEX2D_ARRAY_UAV;
typedef struct D3D11_TEX3D_UAV { UINT MipSlice; UINT FirstWSlice; UINT WSize; } D3D11_TEX3D_UAV;

typedef struct D3D11_UNORDERED_ACCESS_VIEW_DESC
{
    DXGI_FORMAT Format;
    D3D11_UAV_DIMENSION ViewDimension;
    union
    {
        D3D11_BUFFER_UAV Buffer;
        D3D11_TEX1D_UAV Texture1D;
        D3D11_TEX1D_ARRAY_UAV Texture1DArray;
        D3D11_TEX2D_UAV Texture2D;
        D3D11_TEX2D_ARRAY_UAV Texture2DArray;
        D3D11_TEX3D_UAV Texture3D;
    };
} D3D11_UNORDERED_ACCESS_VIEW_DESC;

typedef struct D3D11_SAMPLER_DESC
{
    D3D11_FILTER Filter;
    D3D11_TEXTURE_ADDRESS_MODE AddressU;
    D3D11_TEXTURE_ADDRESS_MODE AddressV;
    D3D11_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    D3D11_COMPARISON_FUNC ComparisonFunc;
    FLOAT BorderColor[4];
    FLOAT MinLOD;
    FLOAT MaxLOD;
} D3D11_SAMPLER_DESC;

typedef struct D3D11_QUERY_DESC
{
    D3D11_QUERY Query;
    UINT MiscFlags;
} D3D11_QUERY_DESC;

typedef struct D3D11_COUNTER_DESC
{
    D3D11_COUNTER Counter;
    UINT MiscFlags;
} D3D11_COUNTER_DESC;

typedef struct D3D11_COUNTER_INFO
{
    D3D11_COUNTER LastDeviceDependentCounter;
    UINT NumSimultaneousCounters;
    UINT8 NumDetectableParallelUnits;
} D3D11_COUNTER_INFO;

typedef struct D3D11_FEATURE_DATA_THREADING
{
    BOOL DriverConcurrentCreates;
    BOOL DriverCommandLists;
} D3D11_FEATURE_DATA_THREADING;

typedef struct D3D11_FEATURE_DATA_DOUBLES
{
    BOOL DoublePrecisionFloatShaderOps;
} D3D11_FEATURE_DATA_DOUBLES;

typedef struct D3D11_FEATURE_DATA_FORMAT_SUPPORT
{
    DXGI_FORMAT InFormat;
    UINT OutFormatSupport;
} D3D11_FEATURE_DATA_FORMAT_SUPPORT;

typedef struct D3D11_FEATURE_DATA_FORMAT_SUPPORT2
{
    DXGI_FORMAT InFormat;
    UINT OutFormatSupport2;
} D3D11_FEATURE_DATA_FORMAT_SUPPORT2;

typedef struct D3D11_CLASS_INSTANCE_DESC
{
    UINT InstanceId;
    UINT InstanceIndex;
    UINT TypeId;
    UINT ConstantBuffer;
    UINT BaseConstantBufferOffset;
    UINT BaseTexture;
    UINT BaseSampler;
    BOOL Created;
} D3D11_CLASS_INSTANCE_DESC;

/* ========================================================================= */
/*                         Interface GUIDs                                   */
/* ========================================================================= */

DEFINE_GUID(IID_ID3D11DeviceChild,      0x1841e5c8,0x16b0,0x489b,0xbc,0xc8,0x44,0xcf,0xb0,0xd5,0xde,0xae);
DEFINE_GUID(IID_ID3D11DepthStencilState,0x03823efb,0x8d8f,0x4e1c,0x9a,0xa2,0xf6,0x4b,0xb2,0xcb,0xfd,0xf1);
DEFINE_GUID(IID_ID3D11BlendState,       0x75b68faa,0x347d,0x4159,0x8f,0x45,0xa0,0x64,0x0f,0x01,0xcd,0x9a);
DEFINE_GUID(IID_ID3D11RasterizerState,  0x9bb4ab81,0xab1a,0x4d8f,0xb5,0x06,0xfc,0x04,0x20,0x0b,0x6e,0xe7);
DEFINE_GUID(IID_ID3D11Resource,         0xdc8e63f3,0xd12b,0x4952,0xb4,0x7b,0x5e,0x45,0x02,0x6a,0x86,0x2d);
DEFINE_GUID(IID_ID3D11Buffer,           0x48570b85,0xd1ee,0x4fcd,0xa2,0x50,0xeb,0x35,0x07,0x22,0xb0,0x37);
DEFINE_GUID(IID_ID3D11Texture1D,        0xf8fb5c27,0xc6b3,0x4f75,0xa4,0xc8,0x43,0x9a,0xf2,0xef,0x56,0x4c);
DEFINE_GUID(IID_ID3D11Texture2D,        0x6f15aaf2,0xd208,0x4e89,0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c);
DEFINE_GUID(IID_ID3D11Texture3D,        0x037e866e,0xf56d,0x4357,0xa8,0xaf,0x9d,0xab,0xbe,0x6e,0x25,0x0e);
DEFINE_GUID(IID_ID3D11View,             0x839d1216,0xbb2e,0x412b,0xb7,0xf4,0xa9,0xdb,0xeb,0xe0,0x8e,0xd1);
DEFINE_GUID(IID_ID3D11ShaderResourceView,0xb0e06fe0,0x8192,0x4e1a,0xb1,0xca,0x36,0xd7,0x41,0x47,0x10,0xb2);
DEFINE_GUID(IID_ID3D11RenderTargetView, 0xdfdba067,0x0b8d,0x4865,0x87,0x5b,0xd7,0xb4,0x51,0x6c,0xc1,0x64);
DEFINE_GUID(IID_ID3D11DepthStencilView, 0x9fdac92a,0x1876,0x48c3,0xaf,0xad,0x25,0xb9,0x4f,0x84,0xa9,0xb6);
DEFINE_GUID(IID_ID3D11UnorderedAccessView,0x28acf509,0x7f5c,0x48f6,0x86,0x11,0xf3,0x16,0x01,0x0a,0x63,0x80);
DEFINE_GUID(IID_ID3D11VertexShader,     0x3b301d64,0xd678,0x4289,0x88,0x97,0x22,0xf8,0x92,0x8b,0x72,0xf3);
DEFINE_GUID(IID_ID3D11HullShader,       0x8e5c6061,0x628a,0x4c8e,0x82,0x64,0xbb,0xe4,0x5c,0xb3,0xd5,0xdd);
DEFINE_GUID(IID_ID3D11DomainShader,     0xf582c508,0x0f36,0x490c,0x99,0x77,0x31,0xee,0xce,0x26,0x8c,0xfa);
DEFINE_GUID(IID_ID3D11GeometryShader,   0x38325b96,0xeffb,0x4022,0xba,0x02,0x2e,0x79,0x5b,0x70,0x27,0x5c);
DEFINE_GUID(IID_ID3D11PixelShader,      0xea82e40d,0x51dc,0x4f33,0x93,0xd4,0xdb,0x7c,0x91,0x25,0xae,0x8c);
DEFINE_GUID(IID_ID3D11ComputeShader,    0x4f5b196e,0xc2bd,0x495e,0xbd,0x01,0x1f,0xde,0xd3,0x8e,0x49,0x69);
DEFINE_GUID(IID_ID3D11InputLayout,      0xe4819ddc,0x4cf0,0x4025,0xbd,0x26,0x5d,0xe8,0x2a,0x3e,0x07,0xb7);
DEFINE_GUID(IID_ID3D11SamplerState,     0xda6fea51,0x564c,0x4487,0x98,0x10,0xf0,0xd0,0xf9,0xb4,0xe3,0xa5);
DEFINE_GUID(IID_ID3D11Asynchronous,     0x4b35d0cd,0x1e15,0x4258,0x9c,0x98,0x1b,0x13,0x33,0xf6,0xdd,0x3b);
DEFINE_GUID(IID_ID3D11Query,            0xd6c00747,0x87b7,0x425e,0xb8,0x4d,0x44,0xd1,0x08,0x56,0x0a,0xfd);
DEFINE_GUID(IID_ID3D11Predicate,        0x9eb576dd,0x9f77,0x4d86,0x81,0xaa,0x8b,0xab,0x5f,0xe4,0x90,0xe2);
DEFINE_GUID(IID_ID3D11Counter,          0x6e8c49fb,0xa371,0x4770,0xb4,0x40,0x29,0x08,0x60,0x22,0xb7,0x41);
DEFINE_GUID(IID_ID3D11ClassInstance,    0xa6cd7faa,0xb0b7,0x4a2f,0x94,0x36,0x86,0x62,0xa6,0x57,0x97,0xcb);
DEFINE_GUID(IID_ID3D11ClassLinkage,     0xddf57cba,0x9543,0x46e4,0xa1,0x2b,0xf2,0x07,0xa0,0xfe,0x7f,0xed);
DEFINE_GUID(IID_ID3D11CommandList,      0xa24bc4d1,0x769e,0x43f7,0x80,0x13,0x98,0xff,0x56,0x6c,0x18,0xe2);
DEFINE_GUID(IID_ID3D11DeviceContext,    0xc0bfa96c,0xe089,0x44fb,0x8e,0xaf,0x26,0xf8,0x79,0x61,0x90,0xda);
DEFINE_GUID(IID_ID3D11Device,           0xdb6f6ddb,0xac77,0x4e88,0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40);

/* ========================================================================= */
/*                             Interfaces                                    */
/* ========================================================================= */

/*
 * ID3D11DeviceChild
 */
#define INTERFACE ID3D11DeviceChild
DECLARE_INTERFACE_(ID3D11DeviceChild, IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

#ifdef COBJMACROS
#define ID3D11DeviceChild_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11DeviceChild_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11DeviceChild_Release(This) (This)->lpVtbl->Release(This)
#define ID3D11DeviceChild_GetDevice(This,dev) (This)->lpVtbl->GetDevice(This,dev)
#define ID3D11DeviceChild_GetPrivateData(This,guid,sz,d) (This)->lpVtbl->GetPrivateData(This,guid,sz,d)
#define ID3D11DeviceChild_SetPrivateData(This,guid,sz,d) (This)->lpVtbl->SetPrivateData(This,guid,sz,d)
#define ID3D11DeviceChild_SetPrivateDataInterface(This,guid,d) (This)->lpVtbl->SetPrivateDataInterface(This,guid,d)
#endif

/*
 * ID3D11DepthStencilState
 */
#define INTERFACE ID3D11DepthStencilState
DECLARE_INTERFACE_(ID3D11DepthStencilState, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_DEPTH_STENCIL_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11BlendState
 */
#define INTERFACE ID3D11BlendState
DECLARE_INTERFACE_(ID3D11BlendState, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_BLEND_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11RasterizerState
 */
#define INTERFACE ID3D11RasterizerState
DECLARE_INTERFACE_(ID3D11RasterizerState, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_RASTERIZER_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11Resource
 */
#define INTERFACE ID3D11Resource
DECLARE_INTERFACE_(ID3D11Resource, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetType)(THIS_ D3D11_RESOURCE_DIMENSION *dimension) PURE;
    STDMETHOD_(void, SetEvictionPriority)(THIS_ UINT priority) PURE;
    STDMETHOD_(UINT, GetEvictionPriority)(THIS) PURE;
};
#undef INTERFACE

/*
 * ID3D11Buffer
 */
#define INTERFACE ID3D11Buffer
DECLARE_INTERFACE_(ID3D11Buffer, ID3D11Resource)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetType)(THIS_ D3D11_RESOURCE_DIMENSION *dimension) PURE;
    STDMETHOD_(void, SetEvictionPriority)(THIS_ UINT priority) PURE;
    STDMETHOD_(UINT, GetEvictionPriority)(THIS) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_BUFFER_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11Texture1D
 */
#define INTERFACE ID3D11Texture1D
DECLARE_INTERFACE_(ID3D11Texture1D, ID3D11Resource)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetType)(THIS_ D3D11_RESOURCE_DIMENSION *dimension) PURE;
    STDMETHOD_(void, SetEvictionPriority)(THIS_ UINT priority) PURE;
    STDMETHOD_(UINT, GetEvictionPriority)(THIS) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_TEXTURE1D_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11Texture2D
 */
#define INTERFACE ID3D11Texture2D
DECLARE_INTERFACE_(ID3D11Texture2D, ID3D11Resource)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetType)(THIS_ D3D11_RESOURCE_DIMENSION *dimension) PURE;
    STDMETHOD_(void, SetEvictionPriority)(THIS_ UINT priority) PURE;
    STDMETHOD_(UINT, GetEvictionPriority)(THIS) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_TEXTURE2D_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11Texture3D
 */
#define INTERFACE ID3D11Texture3D
DECLARE_INTERFACE_(ID3D11Texture3D, ID3D11Resource)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetType)(THIS_ D3D11_RESOURCE_DIMENSION *dimension) PURE;
    STDMETHOD_(void, SetEvictionPriority)(THIS_ UINT priority) PURE;
    STDMETHOD_(UINT, GetEvictionPriority)(THIS) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_TEXTURE3D_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11View
 */
#define INTERFACE ID3D11View
DECLARE_INTERFACE_(ID3D11View, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetResource)(THIS_ ID3D11Resource **resource) PURE;
};
#undef INTERFACE

/*
 * ID3D11ShaderResourceView
 */
#define INTERFACE ID3D11ShaderResourceView
DECLARE_INTERFACE_(ID3D11ShaderResourceView, ID3D11View)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetResource)(THIS_ ID3D11Resource **resource) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_SHADER_RESOURCE_VIEW_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11RenderTargetView
 */
#define INTERFACE ID3D11RenderTargetView
DECLARE_INTERFACE_(ID3D11RenderTargetView, ID3D11View)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetResource)(THIS_ ID3D11Resource **resource) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_RENDER_TARGET_VIEW_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11DepthStencilView
 */
#define INTERFACE ID3D11DepthStencilView
DECLARE_INTERFACE_(ID3D11DepthStencilView, ID3D11View)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetResource)(THIS_ ID3D11Resource **resource) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_DEPTH_STENCIL_VIEW_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11UnorderedAccessView
 */
#define INTERFACE ID3D11UnorderedAccessView
DECLARE_INTERFACE_(ID3D11UnorderedAccessView, ID3D11View)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetResource)(THIS_ ID3D11Resource **resource) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_UNORDERED_ACCESS_VIEW_DESC *desc) PURE;
};
#undef INTERFACE

/* Shader interfaces - all are ID3D11DeviceChild with no additional methods */
#define INTERFACE ID3D11VertexShader
DECLARE_INTERFACE_(ID3D11VertexShader, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

#define INTERFACE ID3D11HullShader
DECLARE_INTERFACE_(ID3D11HullShader, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

#define INTERFACE ID3D11DomainShader
DECLARE_INTERFACE_(ID3D11DomainShader, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

#define INTERFACE ID3D11GeometryShader
DECLARE_INTERFACE_(ID3D11GeometryShader, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

#define INTERFACE ID3D11PixelShader
DECLARE_INTERFACE_(ID3D11PixelShader, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

#define INTERFACE ID3D11ComputeShader
DECLARE_INTERFACE_(ID3D11ComputeShader, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

/*
 * ID3D11InputLayout
 */
#define INTERFACE ID3D11InputLayout
DECLARE_INTERFACE_(ID3D11InputLayout, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
};
#undef INTERFACE

/*
 * ID3D11SamplerState
 */
#define INTERFACE ID3D11SamplerState
DECLARE_INTERFACE_(ID3D11SamplerState, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_SAMPLER_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11Asynchronous
 */
#define INTERFACE ID3D11Asynchronous
DECLARE_INTERFACE_(ID3D11Asynchronous, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(UINT, GetDataSize)(THIS) PURE;
};
#undef INTERFACE

/*
 * ID3D11Query
 */
#define INTERFACE ID3D11Query
DECLARE_INTERFACE_(ID3D11Query, ID3D11Asynchronous)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(UINT, GetDataSize)(THIS) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_QUERY_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11Predicate - same vtbl as ID3D11Query
 */
#ifndef __ID3D11Predicate_FWD_DEFINED__
#define __ID3D11Predicate_FWD_DEFINED__
typedef ID3D11Query ID3D11Predicate;
#endif

/*
 * ID3D11Counter
 */
#define INTERFACE ID3D11Counter
DECLARE_INTERFACE_(ID3D11Counter, ID3D11Asynchronous)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(UINT, GetDataSize)(THIS) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_COUNTER_DESC *desc) PURE;
};
#undef INTERFACE

/*
 * ID3D11ClassInstance
 */
#define INTERFACE ID3D11ClassInstance
DECLARE_INTERFACE_(ID3D11ClassInstance, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(void, GetClassLinkage)(THIS_ ID3D11ClassLinkage **linkage) PURE;
    STDMETHOD_(void, GetDesc)(THIS_ D3D11_CLASS_INSTANCE_DESC *desc) PURE;
    STDMETHOD_(void, GetInstanceName)(THIS_ LPSTR name, SIZE_T *length) PURE;
    STDMETHOD_(void, GetTypeName)(THIS_ LPSTR name, SIZE_T *length) PURE;
};
#undef INTERFACE

/*
 * ID3D11ClassLinkage
 */
#define INTERFACE ID3D11ClassLinkage
DECLARE_INTERFACE_(ID3D11ClassLinkage, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD(GetClassInstance)(THIS_ LPCSTR name, UINT index, ID3D11ClassInstance **instance) PURE;
    STDMETHOD(CreateClassInstance)(THIS_ LPCSTR type_name, UINT cb_offset, UINT cb_vector_offset, UINT texture_offset, UINT sampler_offset, ID3D11ClassInstance **instance) PURE;
};
#undef INTERFACE

/*
 * ID3D11CommandList
 */
#define INTERFACE ID3D11CommandList
DECLARE_INTERFACE_(ID3D11CommandList, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(UINT, GetContextFlags)(THIS) PURE;
};
#undef INTERFACE

/*
 * ID3D11DeviceContext
 */
#define INTERFACE ID3D11DeviceContext
DECLARE_INTERFACE_(ID3D11DeviceContext, ID3D11DeviceChild)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD_(void, GetDevice)(THIS_ ID3D11Device **device) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    /* DeviceContext methods */
    STDMETHOD_(void, VSSetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer *const *buffers) PURE;
    STDMETHOD_(void, PSSetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView *const *views) PURE;
    STDMETHOD_(void, PSSetShader)(THIS_ ID3D11PixelShader *shader, ID3D11ClassInstance *const *instances, UINT count) PURE;
    STDMETHOD_(void, PSSetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState *const *samplers) PURE;
    STDMETHOD_(void, VSSetShader)(THIS_ ID3D11VertexShader *shader, ID3D11ClassInstance *const *instances, UINT count) PURE;
    STDMETHOD_(void, DrawIndexed)(THIS_ UINT index_count, UINT start_index, INT base_vertex) PURE;
    STDMETHOD_(void, Draw)(THIS_ UINT vertex_count, UINT start_vertex) PURE;
    STDMETHOD(Map)(THIS_ ID3D11Resource *resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped) PURE;
    STDMETHOD_(void, Unmap)(THIS_ ID3D11Resource *resource, UINT subresource) PURE;
    STDMETHOD_(void, PSSetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer *const *buffers) PURE;
    STDMETHOD_(void, IASetInputLayout)(THIS_ ID3D11InputLayout *layout) PURE;
    STDMETHOD_(void, IASetVertexBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer *const *buffers, const UINT *strides, const UINT *offsets) PURE;
    STDMETHOD_(void, IASetIndexBuffer)(THIS_ ID3D11Buffer *buffer, DXGI_FORMAT format, UINT offset) PURE;
    STDMETHOD_(void, DrawIndexedInstanced)(THIS_ UINT index_count_per_instance, UINT instance_count, UINT start_index, INT base_vertex, UINT start_instance) PURE;
    STDMETHOD_(void, DrawInstanced)(THIS_ UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex, UINT start_instance) PURE;
    STDMETHOD_(void, GSSetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer *const *buffers) PURE;
    STDMETHOD_(void, GSSetShader)(THIS_ ID3D11GeometryShader *shader, ID3D11ClassInstance *const *instances, UINT count) PURE;
    STDMETHOD_(void, IASetPrimitiveTopology)(THIS_ D3D11_PRIMITIVE_TOPOLOGY topology) PURE;
    STDMETHOD_(void, VSSetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView *const *views) PURE;
    STDMETHOD_(void, VSSetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState *const *samplers) PURE;
    STDMETHOD_(void, Begin)(THIS_ ID3D11Asynchronous *async) PURE;
    STDMETHOD_(void, End)(THIS_ ID3D11Asynchronous *async) PURE;
    STDMETHOD(GetData)(THIS_ ID3D11Asynchronous *async, void *data, UINT data_size, UINT flags) PURE;
    STDMETHOD_(void, SetPredication)(THIS_ ID3D11Predicate *predicate, BOOL value) PURE;
    STDMETHOD_(void, GSSetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView *const *views) PURE;
    STDMETHOD_(void, GSSetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState *const *samplers) PURE;
    STDMETHOD_(void, OMSetRenderTargets)(THIS_ UINT count, ID3D11RenderTargetView *const *rtvs, ID3D11DepthStencilView *dsv) PURE;
    STDMETHOD_(void, OMSetRenderTargetsAndUnorderedAccessViews)(THIS_ UINT rtv_count, ID3D11RenderTargetView *const *rtvs, ID3D11DepthStencilView *dsv, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView *const *uavs, const UINT *initial_counts) PURE;
    STDMETHOD_(void, OMSetBlendState)(THIS_ ID3D11BlendState *state, const FLOAT blend_factor[4], UINT sample_mask) PURE;
    STDMETHOD_(void, OMSetDepthStencilState)(THIS_ ID3D11DepthStencilState *state, UINT stencil_ref) PURE;
    STDMETHOD_(void, SOSetTargets)(THIS_ UINT count, ID3D11Buffer *const *buffers, const UINT *offsets) PURE;
    STDMETHOD_(void, DrawAuto)(THIS) PURE;
    STDMETHOD_(void, DrawIndexedInstancedIndirect)(THIS_ ID3D11Buffer *buffer, UINT offset) PURE;
    STDMETHOD_(void, DrawInstancedIndirect)(THIS_ ID3D11Buffer *buffer, UINT offset) PURE;
    STDMETHOD_(void, Dispatch)(THIS_ UINT x, UINT y, UINT z) PURE;
    STDMETHOD_(void, DispatchIndirect)(THIS_ ID3D11Buffer *buffer, UINT offset) PURE;
    STDMETHOD_(void, RSSetState)(THIS_ ID3D11RasterizerState *state) PURE;
    STDMETHOD_(void, RSSetViewports)(THIS_ UINT count, const D3D11_VIEWPORT *viewports) PURE;
    STDMETHOD_(void, RSSetScissorRects)(THIS_ UINT count, const D3D11_RECT *rects) PURE;
    STDMETHOD_(void, CopySubresourceRegion)(THIS_ ID3D11Resource *dst, UINT dst_subresource, UINT dst_x, UINT dst_y, UINT dst_z, ID3D11Resource *src, UINT src_subresource, const D3D11_BOX *src_box) PURE;
    STDMETHOD_(void, CopyResource)(THIS_ ID3D11Resource *dst, ID3D11Resource *src) PURE;
    STDMETHOD_(void, UpdateSubresource)(THIS_ ID3D11Resource *resource, UINT subresource, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch) PURE;
    STDMETHOD_(void, CopyStructureCount)(THIS_ ID3D11Buffer *dst, UINT dst_offset, ID3D11UnorderedAccessView *src) PURE;
    STDMETHOD_(void, ClearRenderTargetView)(THIS_ ID3D11RenderTargetView *view, const FLOAT color[4]) PURE;
    STDMETHOD_(void, ClearUnorderedAccessViewUint)(THIS_ ID3D11UnorderedAccessView *view, const UINT values[4]) PURE;
    STDMETHOD_(void, ClearUnorderedAccessViewFloat)(THIS_ ID3D11UnorderedAccessView *view, const FLOAT values[4]) PURE;
    STDMETHOD_(void, ClearDepthStencilView)(THIS_ ID3D11DepthStencilView *view, UINT flags, FLOAT depth, UINT8 stencil) PURE;
    STDMETHOD_(void, GenerateMips)(THIS_ ID3D11ShaderResourceView *view) PURE;
    STDMETHOD_(void, SetResourceMinLOD)(THIS_ ID3D11Resource *resource, FLOAT min_lod) PURE;
    STDMETHOD_(FLOAT, GetResourceMinLOD)(THIS_ ID3D11Resource *resource) PURE;
    STDMETHOD_(void, ResolveSubresource)(THIS_ ID3D11Resource *dst, UINT dst_subresource, ID3D11Resource *src, UINT src_subresource, DXGI_FORMAT format) PURE;
    STDMETHOD_(void, ExecuteCommandList)(THIS_ ID3D11CommandList *list, BOOL restore) PURE;
    STDMETHOD_(void, HSSetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView *const *views) PURE;
    STDMETHOD_(void, HSSetShader)(THIS_ ID3D11HullShader *shader, ID3D11ClassInstance *const *instances, UINT count) PURE;
    STDMETHOD_(void, HSSetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState *const *samplers) PURE;
    STDMETHOD_(void, HSSetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer *const *buffers) PURE;
    STDMETHOD_(void, DSSetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView *const *views) PURE;
    STDMETHOD_(void, DSSetShader)(THIS_ ID3D11DomainShader *shader, ID3D11ClassInstance *const *instances, UINT count) PURE;
    STDMETHOD_(void, DSSetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState *const *samplers) PURE;
    STDMETHOD_(void, DSSetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer *const *buffers) PURE;
    STDMETHOD_(void, CSSetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView *const *views) PURE;
    STDMETHOD_(void, CSSetUnorderedAccessViews)(THIS_ UINT start, UINT count, ID3D11UnorderedAccessView *const *views, const UINT *initial_counts) PURE;
    STDMETHOD_(void, CSSetShader)(THIS_ ID3D11ComputeShader *shader, ID3D11ClassInstance *const *instances, UINT count) PURE;
    STDMETHOD_(void, CSSetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState *const *samplers) PURE;
    STDMETHOD_(void, CSSetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer *const *buffers) PURE;
    STDMETHOD_(void, VSGetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer **buffers) PURE;
    STDMETHOD_(void, PSGetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView **views) PURE;
    STDMETHOD_(void, PSGetShader)(THIS_ ID3D11PixelShader **shader, ID3D11ClassInstance **instances, UINT *count) PURE;
    STDMETHOD_(void, PSGetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState **samplers) PURE;
    STDMETHOD_(void, VSGetShader)(THIS_ ID3D11VertexShader **shader, ID3D11ClassInstance **instances, UINT *count) PURE;
    STDMETHOD_(void, PSGetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer **buffers) PURE;
    STDMETHOD_(void, IAGetInputLayout)(THIS_ ID3D11InputLayout **layout) PURE;
    STDMETHOD_(void, IAGetVertexBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer **buffers, UINT *strides, UINT *offsets) PURE;
    STDMETHOD_(void, IAGetIndexBuffer)(THIS_ ID3D11Buffer **buffer, DXGI_FORMAT *format, UINT *offset) PURE;
    STDMETHOD_(void, GSGetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer **buffers) PURE;
    STDMETHOD_(void, GSGetShader)(THIS_ ID3D11GeometryShader **shader, ID3D11ClassInstance **instances, UINT *count) PURE;
    STDMETHOD_(void, IAGetPrimitiveTopology)(THIS_ D3D11_PRIMITIVE_TOPOLOGY *topology) PURE;
    STDMETHOD_(void, VSGetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView **views) PURE;
    STDMETHOD_(void, VSGetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState **samplers) PURE;
    STDMETHOD_(void, GetPredication)(THIS_ ID3D11Predicate **predicate, BOOL *value) PURE;
    STDMETHOD_(void, GSGetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView **views) PURE;
    STDMETHOD_(void, GSGetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState **samplers) PURE;
    STDMETHOD_(void, OMGetRenderTargets)(THIS_ UINT count, ID3D11RenderTargetView **rtvs, ID3D11DepthStencilView **dsv) PURE;
    STDMETHOD_(void, OMGetRenderTargetsAndUnorderedAccessViews)(THIS_ UINT rtv_count, ID3D11RenderTargetView **rtvs, ID3D11DepthStencilView **dsv, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView **uavs) PURE;
    STDMETHOD_(void, OMGetBlendState)(THIS_ ID3D11BlendState **state, FLOAT blend_factor[4], UINT *sample_mask) PURE;
    STDMETHOD_(void, OMGetDepthStencilState)(THIS_ ID3D11DepthStencilState **state, UINT *stencil_ref) PURE;
    STDMETHOD_(void, SOGetTargets)(THIS_ UINT count, ID3D11Buffer **buffers) PURE;
    STDMETHOD_(void, RSGetState)(THIS_ ID3D11RasterizerState **state) PURE;
    STDMETHOD_(void, RSGetViewports)(THIS_ UINT *count, D3D11_VIEWPORT *viewports) PURE;
    STDMETHOD_(void, RSGetScissorRects)(THIS_ UINT *count, D3D11_RECT *rects) PURE;
    STDMETHOD_(void, HSGetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView **views) PURE;
    STDMETHOD_(void, HSGetShader)(THIS_ ID3D11HullShader **shader, ID3D11ClassInstance **instances, UINT *count) PURE;
    STDMETHOD_(void, HSGetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState **samplers) PURE;
    STDMETHOD_(void, HSGetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer **buffers) PURE;
    STDMETHOD_(void, DSGetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView **views) PURE;
    STDMETHOD_(void, DSGetShader)(THIS_ ID3D11DomainShader **shader, ID3D11ClassInstance **instances, UINT *count) PURE;
    STDMETHOD_(void, DSGetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState **samplers) PURE;
    STDMETHOD_(void, DSGetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer **buffers) PURE;
    STDMETHOD_(void, CSGetShaderResources)(THIS_ UINT start, UINT count, ID3D11ShaderResourceView **views) PURE;
    STDMETHOD_(void, CSGetUnorderedAccessViews)(THIS_ UINT start, UINT count, ID3D11UnorderedAccessView **views) PURE;
    STDMETHOD_(void, CSGetShader)(THIS_ ID3D11ComputeShader **shader, ID3D11ClassInstance **instances, UINT *count) PURE;
    STDMETHOD_(void, CSGetSamplers)(THIS_ UINT start, UINT count, ID3D11SamplerState **samplers) PURE;
    STDMETHOD_(void, CSGetConstantBuffers)(THIS_ UINT start, UINT count, ID3D11Buffer **buffers) PURE;
    STDMETHOD_(void, ClearState)(THIS) PURE;
    STDMETHOD_(void, Flush)(THIS) PURE;
    STDMETHOD_(D3D11_DEVICE_CONTEXT_TYPE, GetType)(THIS) PURE;
    STDMETHOD_(UINT, GetContextFlags)(THIS) PURE;
    STDMETHOD(FinishCommandList)(THIS_ BOOL restore, ID3D11CommandList **list) PURE;
};
#undef INTERFACE

#ifdef COBJMACROS
#define ID3D11DeviceContext_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11DeviceContext_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11DeviceContext_Release(This) (This)->lpVtbl->Release(This)
#define ID3D11DeviceContext_VSSetConstantBuffers(This,s,c,b) (This)->lpVtbl->VSSetConstantBuffers(This,s,c,b)
#define ID3D11DeviceContext_PSSetShader(This,sh,inst,cnt) (This)->lpVtbl->PSSetShader(This,sh,inst,cnt)
#define ID3D11DeviceContext_VSSetShader(This,sh,inst,cnt) (This)->lpVtbl->VSSetShader(This,sh,inst,cnt)
#define ID3D11DeviceContext_Draw(This,vc,sv) (This)->lpVtbl->Draw(This,vc,sv)
#define ID3D11DeviceContext_DrawIndexed(This,ic,si,bv) (This)->lpVtbl->DrawIndexed(This,ic,si,bv)
#define ID3D11DeviceContext_Map(This,res,sub,type,fl,map) (This)->lpVtbl->Map(This,res,sub,type,fl,map)
#define ID3D11DeviceContext_Unmap(This,res,sub) (This)->lpVtbl->Unmap(This,res,sub)
#define ID3D11DeviceContext_IASetInputLayout(This,l) (This)->lpVtbl->IASetInputLayout(This,l)
#define ID3D11DeviceContext_IASetVertexBuffers(This,s,c,b,st,o) (This)->lpVtbl->IASetVertexBuffers(This,s,c,b,st,o)
#define ID3D11DeviceContext_IASetIndexBuffer(This,b,f,o) (This)->lpVtbl->IASetIndexBuffer(This,b,f,o)
#define ID3D11DeviceContext_IASetPrimitiveTopology(This,t) (This)->lpVtbl->IASetPrimitiveTopology(This,t)
#define ID3D11DeviceContext_IAGetPrimitiveTopology(This,t) (This)->lpVtbl->IAGetPrimitiveTopology(This,t)
#define ID3D11DeviceContext_OMSetRenderTargets(This,c,r,d) (This)->lpVtbl->OMSetRenderTargets(This,c,r,d)
#define ID3D11DeviceContext_OMSetBlendState(This,s,f,m) (This)->lpVtbl->OMSetBlendState(This,s,f,m)
#define ID3D11DeviceContext_OMSetDepthStencilState(This,s,r) (This)->lpVtbl->OMSetDepthStencilState(This,s,r)
#define ID3D11DeviceContext_RSSetState(This,s) (This)->lpVtbl->RSSetState(This,s)
#define ID3D11DeviceContext_RSSetViewports(This,c,v) (This)->lpVtbl->RSSetViewports(This,c,v)
#define ID3D11DeviceContext_RSSetScissorRects(This,c,r) (This)->lpVtbl->RSSetScissorRects(This,c,r)
#define ID3D11DeviceContext_RSGetViewports(This,c,v) (This)->lpVtbl->RSGetViewports(This,c,v)
#define ID3D11DeviceContext_ClearRenderTargetView(This,v,col) (This)->lpVtbl->ClearRenderTargetView(This,v,col)
#define ID3D11DeviceContext_ClearDepthStencilView(This,v,f,d,s) (This)->lpVtbl->ClearDepthStencilView(This,v,f,d,s)
#define ID3D11DeviceContext_UpdateSubresource(This,r,s,b,d,rp,dp) (This)->lpVtbl->UpdateSubresource(This,r,s,b,d,rp,dp)
#define ID3D11DeviceContext_CopyResource(This,d,s) (This)->lpVtbl->CopyResource(This,d,s)
#define ID3D11DeviceContext_Flush(This) (This)->lpVtbl->Flush(This)
#define ID3D11DeviceContext_ClearState(This) (This)->lpVtbl->ClearState(This)
#define ID3D11DeviceContext_GetType(This) (This)->lpVtbl->GetType(This)
#endif

/*
 * ID3D11Device
 */
#define INTERFACE ID3D11Device
DECLARE_INTERFACE_(ID3D11Device, IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **out) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(CreateBuffer)(THIS_ const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Buffer **buffer) PURE;
    STDMETHOD(CreateTexture1D)(THIS_ const D3D11_TEXTURE1D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture1D **texture) PURE;
    STDMETHOD(CreateTexture2D)(THIS_ const D3D11_TEXTURE2D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture2D **texture) PURE;
    STDMETHOD(CreateTexture3D)(THIS_ const D3D11_TEXTURE3D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture3D **texture) PURE;
    STDMETHOD(CreateShaderResourceView)(THIS_ ID3D11Resource *resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, ID3D11ShaderResourceView **view) PURE;
    STDMETHOD(CreateUnorderedAccessView)(THIS_ ID3D11Resource *resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc, ID3D11UnorderedAccessView **view) PURE;
    STDMETHOD(CreateRenderTargetView)(THIS_ ID3D11Resource *resource, const D3D11_RENDER_TARGET_VIEW_DESC *desc, ID3D11RenderTargetView **view) PURE;
    STDMETHOD(CreateDepthStencilView)(THIS_ ID3D11Resource *resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *desc, ID3D11DepthStencilView **view) PURE;
    STDMETHOD(CreateInputLayout)(THIS_ const D3D11_INPUT_ELEMENT_DESC *descs, UINT count, const void *bytecode, SIZE_T bytecode_length, ID3D11InputLayout **layout) PURE;
    STDMETHOD(CreateVertexShader)(THIS_ const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11VertexShader **shader) PURE;
    STDMETHOD(CreateGeometryShader)(THIS_ const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11GeometryShader **shader) PURE;
    STDMETHOD(CreateGeometryShaderWithStreamOutput)(THIS_ const void *bytecode, SIZE_T length, const D3D11_SO_DECLARATION_ENTRY *so_entries, UINT entry_count, const UINT *strides, UINT stride_count, UINT rasterized_stream, ID3D11ClassLinkage *linkage, ID3D11GeometryShader **shader) PURE;
    STDMETHOD(CreatePixelShader)(THIS_ const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11PixelShader **shader) PURE;
    STDMETHOD(CreateHullShader)(THIS_ const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11HullShader **shader) PURE;
    STDMETHOD(CreateDomainShader)(THIS_ const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11DomainShader **shader) PURE;
    STDMETHOD(CreateComputeShader)(THIS_ const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11ComputeShader **shader) PURE;
    STDMETHOD(CreateClassLinkage)(THIS_ ID3D11ClassLinkage **linkage) PURE;
    STDMETHOD(CreateBlendState)(THIS_ const D3D11_BLEND_DESC *desc, ID3D11BlendState **state) PURE;
    STDMETHOD(CreateDepthStencilState)(THIS_ const D3D11_DEPTH_STENCIL_DESC *desc, ID3D11DepthStencilState **state) PURE;
    STDMETHOD(CreateRasterizerState)(THIS_ const D3D11_RASTERIZER_DESC *desc, ID3D11RasterizerState **state) PURE;
    STDMETHOD(CreateSamplerState)(THIS_ const D3D11_SAMPLER_DESC *desc, ID3D11SamplerState **state) PURE;
    STDMETHOD(CreateQuery)(THIS_ const D3D11_QUERY_DESC *desc, ID3D11Query **query) PURE;
    STDMETHOD(CreatePredicate)(THIS_ const D3D11_QUERY_DESC *desc, ID3D11Predicate **predicate) PURE;
    STDMETHOD(CreateCounter)(THIS_ const D3D11_COUNTER_DESC *desc, ID3D11Counter **counter) PURE;
    STDMETHOD(CreateDeferredContext)(THIS_ UINT flags, ID3D11DeviceContext **context) PURE;
    STDMETHOD(OpenSharedResource)(THIS_ HANDLE resource, REFIID riid, void **out) PURE;
    STDMETHOD(CheckFormatSupport)(THIS_ DXGI_FORMAT format, UINT *support) PURE;
    STDMETHOD(CheckMultisampleQualityLevels)(THIS_ DXGI_FORMAT format, UINT sample_count, UINT *quality_levels) PURE;
    STDMETHOD_(void, CheckCounterInfo)(THIS_ D3D11_COUNTER_INFO *info) PURE;
    STDMETHOD(CheckCounter)(THIS_ const D3D11_COUNTER_DESC *desc, D3D11_COUNTER_TYPE *type, UINT *active_counters, LPSTR name, UINT *name_length, LPSTR units, UINT *units_length, LPSTR description, UINT *description_length) PURE;
    STDMETHOD(CheckFeatureSupport)(THIS_ D3D11_FEATURE feature, void *data, UINT data_size) PURE;
    STDMETHOD(GetPrivateData)(THIS_ REFGUID guid, UINT *data_size, void *data) PURE;
    STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, UINT data_size, const void *data) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ REFGUID guid, const IUnknown *data) PURE;
    STDMETHOD_(D3D_FEATURE_LEVEL, GetFeatureLevel)(THIS) PURE;
    STDMETHOD_(UINT, GetCreationFlags)(THIS) PURE;
    STDMETHOD(GetDeviceRemovedReason)(THIS) PURE;
    STDMETHOD_(void, GetImmediateContext)(THIS_ ID3D11DeviceContext **context) PURE;
    STDMETHOD(SetExceptionMode)(THIS_ UINT flags) PURE;
    STDMETHOD_(UINT, GetExceptionMode)(THIS) PURE;
};
#undef INTERFACE

#ifdef COBJMACROS
#define ID3D11Device_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11Device_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11Device_Release(This) (This)->lpVtbl->Release(This)
#define ID3D11Device_CreateBuffer(This,d,init,b) (This)->lpVtbl->CreateBuffer(This,d,init,b)
#define ID3D11Device_CreateTexture2D(This,d,init,t) (This)->lpVtbl->CreateTexture2D(This,d,init,t)
#define ID3D11Device_CreateShaderResourceView(This,r,d,v) (This)->lpVtbl->CreateShaderResourceView(This,r,d,v)
#define ID3D11Device_CreateRenderTargetView(This,r,d,v) (This)->lpVtbl->CreateRenderTargetView(This,r,d,v)
#define ID3D11Device_CreateDepthStencilView(This,r,d,v) (This)->lpVtbl->CreateDepthStencilView(This,r,d,v)
#define ID3D11Device_CreateInputLayout(This,d,c,bc,bl,l) (This)->lpVtbl->CreateInputLayout(This,d,c,bc,bl,l)
#define ID3D11Device_CreateVertexShader(This,bc,l,lnk,sh) (This)->lpVtbl->CreateVertexShader(This,bc,l,lnk,sh)
#define ID3D11Device_CreatePixelShader(This,bc,l,lnk,sh) (This)->lpVtbl->CreatePixelShader(This,bc,l,lnk,sh)
#define ID3D11Device_CreateBlendState(This,d,s) (This)->lpVtbl->CreateBlendState(This,d,s)
#define ID3D11Device_CreateDepthStencilState(This,d,s) (This)->lpVtbl->CreateDepthStencilState(This,d,s)
#define ID3D11Device_CreateRasterizerState(This,d,s) (This)->lpVtbl->CreateRasterizerState(This,d,s)
#define ID3D11Device_CreateSamplerState(This,d,s) (This)->lpVtbl->CreateSamplerState(This,d,s)
#define ID3D11Device_CreateQuery(This,d,q) (This)->lpVtbl->CreateQuery(This,d,q)
#define ID3D11Device_CheckFormatSupport(This,f,s) (This)->lpVtbl->CheckFormatSupport(This,f,s)
#define ID3D11Device_CheckFeatureSupport(This,f,d,s) (This)->lpVtbl->CheckFeatureSupport(This,f,d,s)
#define ID3D11Device_GetFeatureLevel(This) (This)->lpVtbl->GetFeatureLevel(This)
#define ID3D11Device_GetCreationFlags(This) (This)->lpVtbl->GetCreationFlags(This)
#define ID3D11Device_GetDeviceRemovedReason(This) (This)->lpVtbl->GetDeviceRemovedReason(This)
#define ID3D11Device_GetImmediateContext(This,ctx) (This)->lpVtbl->GetImmediateContext(This,ctx)
#endif

/* ========================================================================= */
/*                         Entry Point Functions                             */
/* ========================================================================= */

HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter *adapter,
    D3D_DRIVER_TYPE driver_type,
    HMODULE swrast,
    UINT flags,
    const D3D_FEATURE_LEVEL *feature_levels,
    UINT levels,
    UINT sdk_version,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *feature_level,
    ID3D11DeviceContext **context);

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter *adapter,
    D3D_DRIVER_TYPE driver_type,
    HMODULE swrast,
    UINT flags,
    const D3D_FEATURE_LEVEL *feature_levels,
    UINT levels,
    UINT sdk_version,
    const DXGI_SWAP_CHAIN_DESC *swapchain_desc,
    IDXGISwapChain **swapchain,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *feature_level,
    ID3D11DeviceContext **context);

/* ========================================================================= */
/*                   Additional COBJMACROS                                   */
/* ========================================================================= */

#ifdef COBJMACROS

/* ID3D11Buffer */
#define ID3D11Buffer_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11Buffer_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11Buffer_Release(This) (This)->lpVtbl->Release(This)
#define ID3D11Buffer_GetDesc(This,desc) (This)->lpVtbl->GetDesc(This,desc)

/* ID3D11Texture1D */
#define ID3D11Texture1D_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11Texture1D_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11Texture1D_Release(This) (This)->lpVtbl->Release(This)
#define ID3D11Texture1D_GetDesc(This,desc) (This)->lpVtbl->GetDesc(This,desc)

/* ID3D11Texture2D */
#define ID3D11Texture2D_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11Texture2D_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11Texture2D_Release(This) (This)->lpVtbl->Release(This)
#define ID3D11Texture2D_GetDesc(This,desc) (This)->lpVtbl->GetDesc(This,desc)

/* ID3D11Texture3D */
#define ID3D11Texture3D_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11Texture3D_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11Texture3D_Release(This) (This)->lpVtbl->Release(This)
#define ID3D11Texture3D_GetDesc(This,desc) (This)->lpVtbl->GetDesc(This,desc)

/* ID3D11Resource */
#define ID3D11Resource_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11Resource_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11Resource_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11ShaderResourceView */
#define ID3D11ShaderResourceView_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11ShaderResourceView_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11ShaderResourceView_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11RenderTargetView */
#define ID3D11RenderTargetView_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11RenderTargetView_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11RenderTargetView_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11DepthStencilView */
#define ID3D11DepthStencilView_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11DepthStencilView_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11DepthStencilView_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11UnorderedAccessView */
#define ID3D11UnorderedAccessView_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11UnorderedAccessView_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11UnorderedAccessView_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11VertexShader */
#define ID3D11VertexShader_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11VertexShader_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11VertexShader_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11PixelShader */
#define ID3D11PixelShader_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11PixelShader_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11PixelShader_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11GeometryShader */
#define ID3D11GeometryShader_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11GeometryShader_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11GeometryShader_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11HullShader */
#define ID3D11HullShader_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11HullShader_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11HullShader_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11DomainShader */
#define ID3D11DomainShader_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11DomainShader_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11DomainShader_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11ComputeShader */
#define ID3D11ComputeShader_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11ComputeShader_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11ComputeShader_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11InputLayout */
#define ID3D11InputLayout_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11InputLayout_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11InputLayout_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11BlendState */
#define ID3D11BlendState_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11BlendState_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11BlendState_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11DepthStencilState */
#define ID3D11DepthStencilState_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11DepthStencilState_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11DepthStencilState_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11RasterizerState */
#define ID3D11RasterizerState_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11RasterizerState_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11RasterizerState_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11SamplerState */
#define ID3D11SamplerState_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11SamplerState_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11SamplerState_Release(This) (This)->lpVtbl->Release(This)

/* ID3D11Query */
#define ID3D11Query_QueryInterface(This,riid,out) (This)->lpVtbl->QueryInterface(This,riid,out)
#define ID3D11Query_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ID3D11Query_Release(This) (This)->lpVtbl->Release(This)

#endif /* COBJMACROS */

#ifdef __cplusplus
}
#endif

#endif /* __d3d11_h__ */
