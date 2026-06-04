/*
 * PROJECT:     ReactOS Software Development Kit
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3D10/D3D10.1 User-Mode Driver DDI (clean-room)
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Clean-room header derived from publicly documented MSDN DDI reference.
 * Layout-sensitive structures match the Windows ABI so that WDDM user-mode
 * drivers (e.g. Mesa Gallium d3d10umd) compile and link correctly.
 *
 * REFERENCES:
 *   - MSDN: D3D10DDIARG_OPENADAPTER, D3D10DDIARG_CREATEDEVICE, etc.
 *   - MSDN: User-Mode Display Driver Functions (D3D10)
 *   - Windows 10 WDK public documentation
 */

#ifndef _D3D10UMDDI_H_
#define _D3D10UMDDI_H_

#include <windef.h>
#include <winerror.h>
#include <dxgitype.h>
#include <dxgiddi.h>
#include <d3d10tokenizedprogramformat.hpp>

#ifdef __cplusplus
extern "C" {
#endif

#pragma warning(push)
#pragma warning(disable:4201) /* nameless struct/union */

/*
 * Generic DDI function pointer slot.  The WDK header uses exact per-slot
 * typedefs for each D3D10DDI_DEVICEFUNCS field.  For our clean-room header
 * we use a pointer-sized integer so any APIENTRY function pointer can be
 * assigned via implicit conversion (the compiler treats UINT_PTR as an
 * integer, so function-ptr-to-integer conversion applies).
 *
 * Mesa compiles with -fpermissive (GCC) or needs -fms-extensions (Clang)
 * to handle the implicit casts.
 */
#ifdef __cplusplus
} /* temporarily close extern "C" for the template struct */
struct D3D10DDI_PFNSLOT {
    void *_ptr;
    D3D10DDI_PFNSLOT() : _ptr(0) {}
    template<typename T> D3D10DDI_PFNSLOT(T fn) { _ptr = reinterpret_cast<void*>(fn); }
    template<typename T> operator T() const { return reinterpret_cast<T>(_ptr); }
    explicit operator bool() const { return _ptr != 0; }
};
extern "C" { /* reopen */
#define D3D10DDI_PFNTYPE D3D10DDI_PFNSLOT
#else
typedef void (*D3D10DDI_PFNSLOT)();
#define D3D10DDI_PFNTYPE D3D10DDI_PFNSLOT
#endif

/* ===================================================================
 *  Version constants
 * =================================================================== */

#define D3D10_DDI_MINOR_HEADER_VERSION 2

#define D3D10_0_DDI_INTERFACE_VERSION   0x0000000A
#define D3D10_0_x_DDI_INTERFACE_VERSION 0x00000010
#define D3D10_0_7_DDI_INTERFACE_VERSION 0x00000017
#define D3D10_1_DDI_INTERFACE_VERSION   0x00000020
#define D3D10_1_x_DDI_INTERFACE_VERSION 0x00000021
#define D3D10_1_7_DDI_INTERFACE_VERSION 0x00000027
#define D3D11_0_DDI_INTERFACE_VERSION   0x00000030
#define D3D11_0_7_DDI_INTERFACE_VERSION 0x00000037

/*
 * "Supported" versions encode (InterfaceVersion << 16 | RuntimeVersion).
 * The runtime fills in the low 16 bits; the high 16 bits are the interface.
 */
#define D3D10_0_DDI_SUPPORTED           ((UINT64)D3D10_0_DDI_INTERFACE_VERSION   << 16)
#define D3D10_0_x_DDI_SUPPORTED         ((UINT64)D3D10_0_x_DDI_INTERFACE_VERSION << 16)
#define D3D10_0_7_DDI_SUPPORTED         ((UINT64)D3D10_0_7_DDI_INTERFACE_VERSION << 16)
#define D3D10_1_DDI_SUPPORTED           ((UINT64)D3D10_1_DDI_INTERFACE_VERSION   << 16)
#define D3D10_1_x_DDI_SUPPORTED         ((UINT64)D3D10_1_x_DDI_INTERFACE_VERSION << 16)
#define D3D10_1_7_DDI_SUPPORTED         ((UINT64)D3D10_1_7_DDI_INTERFACE_VERSION << 16)
#define D3D11_0_DDI_SUPPORTED           ((UINT64)D3D11_0_DDI_INTERFACE_VERSION   << 16)
#define D3D11_0_7_DDI_SUPPORTED         ((UINT64)D3D11_0_7_DDI_INTERFACE_VERSION << 16)

/* ===================================================================
 *  Limits
 * =================================================================== */

#define D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT 8
#define D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE 16

/* ===================================================================
 *  Handle types — opaque driver-private pointers
 * =================================================================== */

typedef struct { void *pDrvPrivate; } D3D10DDI_HADAPTER;
typedef struct { void *pDrvPrivate; } D3D10DDI_HDEVICE;
typedef struct { void *pDrvPrivate; } D3D10DDI_HRESOURCE;
typedef struct { void *pDrvPrivate; } D3D10DDI_HSHADERRESOURCEVIEW;
typedef struct { void *pDrvPrivate; } D3D10DDI_HRENDERTARGETVIEW;
typedef struct { void *pDrvPrivate; } D3D10DDI_HDEPTHSTENCILVIEW;
typedef struct { void *pDrvPrivate; } D3D10DDI_HSHADER;
typedef struct { void *pDrvPrivate; } D3D10DDI_HELEMENTLAYOUT;
typedef struct { void *pDrvPrivate; } D3D10DDI_HBLENDSTATE;
typedef struct { void *pDrvPrivate; } D3D10DDI_HDEPTHSTENCILSTATE;
typedef struct { void *pDrvPrivate; } D3D10DDI_HRASTERIZERSTATE;
typedef struct { void *pDrvPrivate; } D3D10DDI_HSAMPLER;
typedef struct { void *pDrvPrivate; } D3D10DDI_HQUERY;

/* Runtime handles — opaque to the driver */
typedef struct { UINT_PTR handle; } D3D10DDI_HRTRESOURCE;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTSHADERRESOURCEVIEW;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTRENDERTARGETVIEW;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTDEPTHSTENCILVIEW;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTSHADER;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTELEMENTLAYOUT;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTBLENDSTATE;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTDEPTHSTENCILSTATE;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTRASTERIZERSTATE;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTSAMPLER;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTQUERY;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTCORELAYER;
typedef struct { UINT_PTR handle; } D3D10DDI_HRTDEVICE;

/* ===================================================================
 *  Enumerations
 * =================================================================== */

typedef enum D3D10DDIRESOURCE_TYPE
{
    D3D10DDIRESOURCE_BUFFER      = 1,
    D3D10DDIRESOURCE_TEXTURE1D   = 2,
    D3D10DDIRESOURCE_TEXTURE2D   = 3,
    D3D10DDIRESOURCE_TEXTURE3D   = 4,
    D3D10DDIRESOURCE_TEXTURECUBE = 5,
} D3D10DDIRESOURCE_TYPE;

typedef enum D3D10_DDI_RESOURCE_USAGE
{
    D3D10_DDI_USAGE_DEFAULT   = 0,
    D3D10_DDI_USAGE_IMMUTABLE = 1,
    D3D10_DDI_USAGE_DYNAMIC   = 2,
    D3D10_DDI_USAGE_STAGING   = 3,
} D3D10_DDI_RESOURCE_USAGE;

typedef enum D3D10_DDI_RESOURCE_BIND_FLAG
{
    D3D10_DDI_BIND_VERTEX_BUFFER    = 0x00000001,
    D3D10_DDI_BIND_INDEX_BUFFER     = 0x00000002,
    D3D10_DDI_BIND_CONSTANT_BUFFER  = 0x00000004,
    D3D10_DDI_BIND_SHADER_RESOURCE  = 0x00000008,
    D3D10_DDI_BIND_STREAM_OUTPUT    = 0x00000010,
    D3D10_DDI_BIND_RENDER_TARGET    = 0x00000020,
    D3D10_DDI_BIND_DEPTH_STENCIL    = 0x00000040,
    D3D10_DDI_BIND_PRESENT          = 0x00000080,
} D3D10_DDI_RESOURCE_BIND_FLAG;

typedef enum D3D10_DDI_RESOURCE_MISC_FLAG
{
    D3D10_DDI_RESOURCE_MISC_SHARED                  = 0x00000002,
    D3D10_DDI_RESOURCE_AUTO_BACK_BUFFER             = 0x00000004,
    D3D10_DDI_RESOURCE_MISC_GDI_COMPATIBLE          = 0x00000008,
    D3D10_DDI_RESOURCE_MISC_GENERATE_MIPS           = 0x00000020,
    D3D10_DDI_RESOURCE_MISC_RESOURCE_CLAMP          = 0x00000080,
    D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED       = 0x00000200,
} D3D10_DDI_RESOURCE_MISC_FLAG;

typedef enum D3D10_DDI_MAP
{
    D3D10_DDI_MAP_READ              = 1,
    D3D10_DDI_MAP_WRITE             = 2,
    D3D10_DDI_MAP_READWRITE         = 3,
    D3D10_DDI_MAP_WRITE_DISCARD     = 4,
    D3D10_DDI_MAP_WRITE_NOOVERWRITE = 5,
} D3D10_DDI_MAP;

#define D3D10_DDI_MAP_FLAG_DONOTWAIT 0x00100000

typedef enum D3D10_DDI_PRIMITIVE_TOPOLOGY
{
    D3D10_DDI_PRIMITIVE_TOPOLOGY_UNDEFINED         = 0,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_POINTLIST         = 1,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST           = 2,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP          = 3,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST       = 4,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP      = 5,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST_ADJ       = 10,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ      = 11,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ   = 12,
    D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ  = 13,
} D3D10_DDI_PRIMITIVE_TOPOLOGY;

/* Blend enums */
typedef enum D3D10_DDI_BLEND
{
    D3D10_DDI_BLEND_ZERO            = 1,
    D3D10_DDI_BLEND_ONE             = 2,
    D3D10_DDI_BLEND_SRC_COLOR       = 3,
    D3D10_DDI_BLEND_INV_SRC_COLOR   = 4,
    D3D10_DDI_BLEND_SRC_ALPHA       = 5,
    D3D10_DDI_BLEND_INV_SRC_ALPHA   = 6,
    D3D10_DDI_BLEND_DEST_ALPHA      = 7,
    D3D10_DDI_BLEND_INV_DEST_ALPHA  = 8,
    D3D10_DDI_BLEND_DEST_COLOR      = 9,
    D3D10_DDI_BLEND_INV_DEST_COLOR  = 10,
    D3D10_DDI_BLEND_SRC_ALPHASAT    = 11,
    D3D10_DDI_BLEND_BLEND_FACTOR    = 14,
    D3D10_DDI_BLEND_INVBLEND_FACTOR = 15,
    D3D10_DDI_BLEND_SRC1_COLOR      = 16,
    D3D10_DDI_BLEND_INV_SRC1_COLOR  = 17,
    D3D10_DDI_BLEND_SRC1_ALPHA      = 18,
    D3D10_DDI_BLEND_INV_SRC1_ALPHA  = 19,
} D3D10_DDI_BLEND;

typedef enum D3D10_DDI_BLEND_OP
{
    D3D10_DDI_BLEND_OP_ADD          = 1,
    D3D10_DDI_BLEND_OP_SUBTRACT     = 2,
    D3D10_DDI_BLEND_OP_REV_SUBTRACT = 3,
    D3D10_DDI_BLEND_OP_MIN          = 4,
    D3D10_DDI_BLEND_OP_MAX          = 5,
} D3D10_DDI_BLEND_OP;

/* Comparison function */
typedef enum D3D10_DDI_COMPARISON_FUNC
{
    D3D10_DDI_COMPARISON_NEVER         = 1,
    D3D10_DDI_COMPARISON_LESS          = 2,
    D3D10_DDI_COMPARISON_EQUAL         = 3,
    D3D10_DDI_COMPARISON_LESS_EQUAL    = 4,
    D3D10_DDI_COMPARISON_GREATER       = 5,
    D3D10_DDI_COMPARISON_NOT_EQUAL     = 6,
    D3D10_DDI_COMPARISON_GREATER_EQUAL = 7,
    D3D10_DDI_COMPARISON_ALWAYS        = 8,
} D3D10_DDI_COMPARISON_FUNC;

/* Stencil operations */
typedef enum D3D10_DDI_STENCIL_OP
{
    D3D10_DDI_STENCIL_OP_KEEP     = 1,
    D3D10_DDI_STENCIL_OP_ZERO     = 2,
    D3D10_DDI_STENCIL_OP_REPLACE  = 3,
    D3D10_DDI_STENCIL_OP_INCR_SAT = 4,
    D3D10_DDI_STENCIL_OP_DECR_SAT = 5,
    D3D10_DDI_STENCIL_OP_INVERT   = 6,
    D3D10_DDI_STENCIL_OP_INCR     = 7,
    D3D10_DDI_STENCIL_OP_DECR     = 8,
} D3D10_DDI_STENCIL_OP;

/* Depth write mask */
typedef enum D3D10_DDI_DEPTH_WRITE_MASK
{
    D3D10_DDI_DEPTH_WRITE_MASK_ZERO = 0,
    D3D10_DDI_DEPTH_WRITE_MASK_ALL  = 1,
} D3D10_DDI_DEPTH_WRITE_MASK;

/* Fill mode */
typedef enum D3D10_DDI_FILL_MODE
{
    D3D10_DDI_FILL_WIREFRAME = 2,
    D3D10_DDI_FILL_SOLID     = 3,
} D3D10_DDI_FILL_MODE;

/* Cull mode */
typedef enum D3D10_DDI_CULL_MODE
{
    D3D10_DDI_CULL_NONE  = 1,
    D3D10_DDI_CULL_FRONT = 2,
    D3D10_DDI_CULL_BACK  = 3,
} D3D10_DDI_CULL_MODE;

/* Texture address mode */
typedef enum D3D10_DDI_TEXTURE_ADDRESS_MODE
{
    D3D10_DDI_TEXTURE_ADDRESS_WRAP       = 1,
    D3D10_DDI_TEXTURE_ADDRESS_MIRROR     = 2,
    D3D10_DDI_TEXTURE_ADDRESS_CLAMP      = 3,
    D3D10_DDI_TEXTURE_ADDRESS_BORDER     = 4,
    D3D10_DDI_TEXTURE_ADDRESS_MIRRORONCE = 5,
} D3D10_DDI_TEXTURE_ADDRESS_MODE;

/* Filter type */
typedef enum D3D10_DDI_FILTER_TYPE
{
    D3D10_DDI_FILTER_TYPE_POINT  = 0,
    D3D10_DDI_FILTER_TYPE_LINEAR = 1,
} D3D10_DDI_FILTER_TYPE;

typedef UINT D3D10_DDI_FILTER;

/* Filter decode macros — encode: (min << 4) | (mag << 2) | mip
 * with anisotropic = 0x40, comparison = 0x80, text_1bit = 0x80000000 */
#define D3D10_DDI_DECODE_MIN_FILTER(f)            ((D3D10_DDI_FILTER_TYPE)(((f) >> 4) & 0x3))
#define D3D10_DDI_DECODE_MAG_FILTER(f)            ((D3D10_DDI_FILTER_TYPE)(((f) >> 2) & 0x3))
#define D3D10_DDI_DECODE_MIP_FILTER(f)            ((D3D10_DDI_FILTER_TYPE)((f) & 0x3))
#define D3D10_DDI_DECODE_IS_ANISOTROPIC_FILTER(f) (((f) & 0x40) != 0)
#define D3D10_DDI_DECODE_IS_COMPARISON_FILTER(f)  (((f) & 0x80) != 0)
#define D3D10_DDI_DECODE_IS_TEXT_1BIT_FILTER(f)   (((f) & 0x80000000u) != 0)

/* Input classification */
typedef enum D3D10_DDI_INPUT_CLASSIFICATION
{
    D3D10_DDI_INPUT_PER_VERTEX_DATA   = 0,
    D3D10_DDI_INPUT_PER_INSTANCE_DATA = 1,
} D3D10_DDI_INPUT_CLASSIFICATION;

/* Query types */
typedef enum D3D10DDI_QUERY
{
    D3D10DDI_QUERY_EVENT                    = 0,
    D3D10DDI_QUERY_OCCLUSION               = 1,
    D3D10DDI_QUERY_TIMESTAMP                = 2,
    D3D10DDI_QUERY_TIMESTAMPDISJOINT        = 3,
    D3D10DDI_QUERY_PIPELINESTATS            = 4,
    D3D10DDI_QUERY_OCCLUSIONPREDICATE       = 5,
    D3D10DDI_QUERY_STREAMOUTPUTSTATS        = 6,
    D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE  = 7,
} D3D10DDI_QUERY;

#define D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT 0x1

/* Counter type */
typedef enum D3D10DDI_COUNTER_TYPE
{
    D3D10DDI_COUNTER_TYPE_FLOAT32 = 0,
    D3D10DDI_COUNTER_TYPE_UINT16  = 1,
    D3D10DDI_COUNTER_TYPE_UINT32  = 2,
    D3D10DDI_COUNTER_TYPE_UINT64  = 3,
} D3D10DDI_COUNTER_TYPE;

/* Clear flags */
#define D3D10_DDI_CLEAR_DEPTH   0x1
#define D3D10_DDI_CLEAR_STENCIL 0x2

/* Format support flags */
#define D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE             0x00000001
#define D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET              0x00000002
#define D3D10_DDI_FORMAT_SUPPORT_BLENDABLE                 0x00000004
#define D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET  0x00000008
#define D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD          0x00000010
#define D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED             0x80000000

/* Query get-data flags */
#define D3D10_DDI_GET_DATA_DO_NOT_FLUSH 0x1

/* DXGI_STATUS_NO_REDIRECTION — returned by CreateDevice to tell DXGI
 * not to use shared-resource presentation path (see MSDN). */
#ifndef DXGI_STATUS_NO_REDIRECTION
#define DXGI_STATUS_NO_REDIRECTION ((HRESULT)0x087A0009L)
#endif

/* ===================================================================
 *  Simple structures
 * =================================================================== */

typedef struct D3D10DDI_MAPPED_SUBRESOURCE
{
    void *pData;
    UINT  RowPitch;
    UINT  DepthPitch;
} D3D10DDI_MAPPED_SUBRESOURCE;

typedef struct D3D10_DDI_BOX
{
    UINT left;
    UINT top;
    UINT front;
    UINT right;
    UINT bottom;
    UINT back;
} D3D10_DDI_BOX;

typedef struct D3D10_DDI_VIEWPORT
{
    FLOAT TopLeftX;
    FLOAT TopLeftY;
    FLOAT Width;
    FLOAT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
} D3D10_DDI_VIEWPORT;

typedef RECT D3D10_DDI_RECT;

/* ===================================================================
 *  Counter info
 * =================================================================== */

typedef struct D3D10DDI_COUNTER_INFO
{
    D3D10DDI_QUERY LastDeviceDependentCounter;
    UINT           NumSimultaneousCounters;
    UINT8          NumDetectableParallelUnits;
} D3D10DDI_COUNTER_INFO;

/* ===================================================================
 *  Query data structures
 * =================================================================== */

typedef struct D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS
{
    UINT64 IAVertices;
    UINT64 IAPrimitives;
    UINT64 VSInvocations;
    UINT64 GSInvocations;
    UINT64 GSPrimitives;
    UINT64 CInvocations;
    UINT64 CPrimitives;
    UINT64 PSInvocations;
} D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS;

typedef struct D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT
{
    UINT64 Frequency;
    BOOL   Disjoint;
} D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT;

typedef struct D3D10_DDI_QUERY_DATA_SO_STATISTICS
{
    UINT64 NumPrimitivesWritten;
    UINT64 PrimitivesStorageNeeded;
} D3D10_DDI_QUERY_DATA_SO_STATISTICS;

/* ===================================================================
 *  Sampler descriptor
 * =================================================================== */

typedef struct D3D10_DDI_SAMPLER_DESC
{
    D3D10_DDI_FILTER               Filter;
    D3D10_DDI_TEXTURE_ADDRESS_MODE AddressU;
    D3D10_DDI_TEXTURE_ADDRESS_MODE AddressV;
    D3D10_DDI_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT                          MipLODBias;
    UINT                           MaxAnisotropy;
    D3D10_DDI_COMPARISON_FUNC      ComparisonFunc;
    FLOAT                          BorderColor[4];
    FLOAT                          MinLOD;
    FLOAT                          MaxLOD;
} D3D10_DDI_SAMPLER_DESC;

/* ===================================================================
 *  Blend state descriptors
 * =================================================================== */

typedef struct D3D10_DDI_BLEND_DESC
{
    BOOL               AlphaToCoverageEnable;
    BOOL               BlendEnable[D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT];
    D3D10_DDI_BLEND    SrcBlend;
    D3D10_DDI_BLEND    DestBlend;
    D3D10_DDI_BLEND_OP BlendOp;
    D3D10_DDI_BLEND    SrcBlendAlpha;
    D3D10_DDI_BLEND    DestBlendAlpha;
    D3D10_DDI_BLEND_OP BlendOpAlpha;
    UINT8              RenderTargetWriteMask[D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT];
} D3D10_DDI_BLEND_DESC;

typedef struct D3D10_DDI_RENDER_TARGET_BLEND_DESC1
{
    BOOL               BlendEnable;
    D3D10_DDI_BLEND    SrcBlend;
    D3D10_DDI_BLEND    DestBlend;
    D3D10_DDI_BLEND_OP BlendOp;
    D3D10_DDI_BLEND    SrcBlendAlpha;
    D3D10_DDI_BLEND    DestBlendAlpha;
    D3D10_DDI_BLEND_OP BlendOpAlpha;
    UINT8              RenderTargetWriteMask;
} D3D10_DDI_RENDER_TARGET_BLEND_DESC1;

typedef struct D3D10_1_DDI_BLEND_DESC
{
    BOOL                                AlphaToCoverageEnable;
    BOOL                                IndependentBlendEnable;
    D3D10_DDI_RENDER_TARGET_BLEND_DESC1 RenderTarget[D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT];
} D3D10_1_DDI_BLEND_DESC;

/* ===================================================================
 *  Depth-stencil descriptor
 * =================================================================== */

typedef struct D3D10_DDI_DEPTH_STENCILOP_DESC
{
    D3D10_DDI_STENCIL_OP      StencilFailOp;
    D3D10_DDI_STENCIL_OP      StencilDepthFailOp;
    D3D10_DDI_STENCIL_OP      StencilPassOp;
    D3D10_DDI_COMPARISON_FUNC StencilFunc;
} D3D10_DDI_DEPTH_STENCILOP_DESC;

typedef struct D3D10_DDI_DEPTH_STENCIL_DESC
{
    BOOL                          DepthEnable;
    D3D10_DDI_DEPTH_WRITE_MASK    DepthWriteMask;
    D3D10_DDI_COMPARISON_FUNC     DepthFunc;
    BOOL                          StencilEnable;
    UINT8                         FrontEnable;
    UINT8                         BackEnable;
    UINT8                         StencilReadMask;
    UINT8                         StencilWriteMask;
    D3D10_DDI_DEPTH_STENCILOP_DESC FrontFace;
    D3D10_DDI_DEPTH_STENCILOP_DESC BackFace;
} D3D10_DDI_DEPTH_STENCIL_DESC;

/* ===================================================================
 *  Rasterizer descriptor
 * =================================================================== */

typedef struct D3D10_DDI_RASTERIZER_DESC
{
    D3D10_DDI_FILL_MODE FillMode;
    D3D10_DDI_CULL_MODE CullMode;
    BOOL                FrontCounterClockwise;
    INT                 DepthBias;
    FLOAT               DepthBiasClamp;
    FLOAT               SlopeScaledDepthBias;
    BOOL                DepthClipEnable;
    BOOL                ScissorEnable;
    BOOL                MultisampleEnable;
    BOOL                AntialiasedLineEnable;
} D3D10_DDI_RASTERIZER_DESC;

/* ===================================================================
 *  Mip info for resource creation
 * =================================================================== */

typedef struct D3D10DDI_MIPINFO
{
    UINT TexelWidth;
    UINT TexelHeight;
    UINT TexelDepth;
    UINT PhysicalWidth;
    UINT PhysicalHeight;
    UINT PhysicalDepth;
} D3D10DDI_MIPINFO;

/* ===================================================================
 *  Resource creation arguments
 * =================================================================== */

typedef struct D3D10_DDIARG_SUBRESOURCE_UP
{
    const void *pSysMem;
    UINT        SysMemPitch;
    UINT        SysMemSlicePitch;
} D3D10_DDIARG_SUBRESOURCE_UP;

typedef struct D3D10DDIARG_CREATERESOURCE
{
    const D3D10DDI_MIPINFO*           pMipInfoList;
    const D3D10_DDIARG_SUBRESOURCE_UP* pInitialDataUP;
    D3D10DDIRESOURCE_TYPE             ResourceDimension;
    D3D10_DDI_RESOURCE_USAGE          Usage;
    UINT                              BindFlags;
    UINT                              MapFlags;
    UINT                              MiscFlags;
    DXGI_FORMAT                       Format;
    DXGI_SAMPLE_DESC                  SampleDesc;
    UINT                              MipLevels;
    UINT                              ArraySize;
    DXGI_DDI_PRIMARY_DESC*            pPrimaryDesc;
} D3D10DDIARG_CREATERESOURCE;

/* ===================================================================
 *  Open resource
 * =================================================================== */

typedef struct D3D10DDIARG_OPENRESOURCE
{
    UINT                   NumAllocations;
    void*                  pOpenAllocationInfo; /* D3D10DDI_OPENALLOCATIONINFO array */
    D3D10DDI_HRTRESOURCE   hKMResource;
    void*                  pPrivateDriverData;
    UINT                   PrivateDriverDataSize;
} D3D10DDIARG_OPENRESOURCE;

/* ===================================================================
 *  Shader resource view creation
 * =================================================================== */

typedef struct D3D10DDIARG_CREATESHADERRESOURCEVIEW
{
    D3D10DDI_HRESOURCE     hDrvResource;
    DXGI_FORMAT            Format;
    D3D10DDIRESOURCE_TYPE  ResourceDimension;
    union
    {
        struct { UINT FirstElement; UINT NumElements; }    Buffer;
        struct { UINT MostDetailedMip; UINT MipLevels;
                 UINT FirstArraySlice; UINT ArraySize; }   Tex1D;
        struct { UINT MostDetailedMip; UINT MipLevels;
                 UINT FirstArraySlice; UINT ArraySize; }   Tex2D;
        struct { UINT MostDetailedMip; UINT MipLevels; }   Tex3D;
        struct { UINT MostDetailedMip; UINT MipLevels; }   TexCube;
    };
} D3D10DDIARG_CREATESHADERRESOURCEVIEW;

typedef struct D3D10_1DDIARG_CREATESHADERRESOURCEVIEW
{
    D3D10DDI_HRESOURCE     hDrvResource;
    DXGI_FORMAT            Format;
    D3D10DDIRESOURCE_TYPE  ResourceDimension;
    union
    {
        struct { UINT FirstElement; UINT NumElements; }    Buffer;
        struct { UINT MostDetailedMip; UINT MipLevels;
                 UINT FirstArraySlice; UINT ArraySize; }   Tex1D;
        struct { UINT MostDetailedMip; UINT MipLevels;
                 UINT FirstArraySlice; UINT ArraySize; }   Tex2D;
        struct { UINT MostDetailedMip; UINT MipLevels; }   Tex3D;
        struct { UINT MostDetailedMip; UINT MipLevels;
                 UINT First2DArrayFace; UINT NumCubes; }   TexCube;
    };
} D3D10_1DDIARG_CREATESHADERRESOURCEVIEW;

/* ===================================================================
 *  Render target / depth stencil view creation
 * =================================================================== */

typedef struct D3D10DDIARG_CREATERENDERTARGETVIEW
{
    D3D10DDI_HRESOURCE     hDrvResource;
    DXGI_FORMAT            Format;
    D3D10DDIRESOURCE_TYPE  ResourceDimension;
    union
    {
        struct { UINT FirstElement; UINT NumElements; }    Buffer;
        struct { UINT MipSlice; UINT FirstArraySlice;
                 UINT ArraySize; }                         Tex1D;
        struct { UINT MipSlice; UINT FirstArraySlice;
                 UINT ArraySize; }                         Tex2D;
        struct { UINT MipSlice; UINT FirstW; UINT WSize; } Tex3D;
        struct { UINT MipSlice; UINT FirstArraySlice;
                 UINT ArraySize; }                         TexCube;
    };
} D3D10DDIARG_CREATERENDERTARGETVIEW;

typedef struct D3D10DDIARG_CREATEDEPTHSTENCILVIEW
{
    D3D10DDI_HRESOURCE     hDrvResource;
    DXGI_FORMAT            Format;
    D3D10DDIRESOURCE_TYPE  ResourceDimension;
    union
    {
        struct { UINT MipSlice; UINT FirstArraySlice;
                 UINT ArraySize; }  Tex1D;
        struct { UINT MipSlice; UINT FirstArraySlice;
                 UINT ArraySize; }  Tex2D;
        struct { UINT MipSlice; UINT FirstArraySlice;
                 UINT ArraySize; }  TexCube;
    };
} D3D10DDIARG_CREATEDEPTHSTENCILVIEW;

/* ===================================================================
 *  Element layout (vertex input layout)
 * =================================================================== */

typedef struct D3D10DDIARG_INPUT_ELEMENT_DESC
{
    UINT                             InputSlot;
    UINT                             AlignedByteOffset;
    DXGI_FORMAT                      Format;
    D3D10_DDI_INPUT_CLASSIFICATION   InputSlotClass;
    UINT                             InstanceDataStepRate;
    UINT                             InputRegister;
} D3D10DDIARG_INPUT_ELEMENT_DESC;

typedef struct D3D10DDIARG_CREATEELEMENTLAYOUT
{
    const D3D10DDIARG_INPUT_ELEMENT_DESC* pVertexElements;
    UINT                                  NumElements;
} D3D10DDIARG_CREATEELEMENTLAYOUT;

/* ===================================================================
 *  Shader creation arguments
 * =================================================================== */

typedef struct D3D10DDIARG_STAGE_IO_SIGNATURES
{
    const void* pInputSignature;
    UINT        NumInputSignatureEntries;
    const void* pOutputSignature;
    UINT        NumOutputSignatureEntries;
} D3D10DDIARG_STAGE_IO_SIGNATURES;

typedef struct D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY
{
    UINT OutputSlot;
    UINT RegisterIndex;
    UINT RegisterMask;      /* same as ComponentCount in some references */
    UINT StartComponent;
    UINT ComponentCount;
} D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY;

typedef struct D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT
{
    const UINT*                                    pShaderCode;
    const D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY* pOutputStreamDecl;
    UINT                                           NumEntries;
    UINT                                           StreamOutputStrideInBytes;
    const D3D10DDIARG_STAGE_IO_SIGNATURES*         pSignatures;
} D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT;

/* ===================================================================
 *  Query creation
 * =================================================================== */

typedef struct D3D10DDIARG_CREATEQUERY
{
    D3D10DDI_QUERY Query;
    UINT           MiscFlags;
} D3D10DDIARG_CREATEQUERY;

/* ===================================================================
 *  CalcPrivateDeviceSize
 * =================================================================== */

typedef struct D3D10DDIARG_CALCPRIVATEDEVICESIZE
{
    UINT Interface;
    UINT Version;
    UINT Flags;
} D3D10DDIARG_CALCPRIVATEDEVICESIZE;

/* ===================================================================
 *  GetCaps (OpenAdapter10_2)
 * =================================================================== */

typedef struct D3D10_2DDIARG_GETCAPS
{
    UINT  Type;
    void* pData;
    UINT  DataSize;
} D3D10_2DDIARG_GETCAPS;

/* ===================================================================
 *  D3DDDI_DEVICECALLBACKS — kernel-mode thunk callbacks provided to UMD
 *  This is the pKTCallbacks table from D3D10DDIARG_CREATEDEVICE.
 *  Only the function pointers actually used need to be present;
 *  the struct is memcpy'd by the driver so size must match ABI.
 * =================================================================== */

typedef HRESULT (APIENTRY *PFND3DDDI_ALLOCATECB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_DEALLOCATECB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SETPRIORITYCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_QUERYRESIDENCYCB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SETDISPLAYMODECB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_PRESENTCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_RENDERCB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_LOCKCB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_UNLOCKCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_ESCAPECB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_CREATEOVERLAYCB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_UPDATEOVERLAYCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_FLIPOVERLAYCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_DESTROYOVERLAYCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_CREATECONTEXTCB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_DESTROYCONTEXTCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_CREATESYNCHRONIZATIONOBJECTCB)(HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_DESTROYSYNCHRONIZATIONOBJECTCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_WAITFORSYNCHRONIZATIONOBJECTCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SIGNALSYNCHRONIZATIONOBJECTCB)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SETASYNCCALLBACKSCB)(HANDLE hDevice, BOOL Enable);
typedef HRESULT (APIENTRY *PFND3DDDI_SETDISPLAYPRIVATEDRIVERFORMATCB)(HANDLE hDevice, const void *pData);

typedef struct D3DDDI_DEVICECALLBACKS
{
    PFND3DDDI_ALLOCATECB                     pfnAllocateCb;
    PFND3DDDI_DEALLOCATECB                   pfnDeallocateCb;
    PFND3DDDI_SETPRIORITYCB                  pfnSetPriorityCb;
    PFND3DDDI_QUERYRESIDENCYCB               pfnQueryResidencyCb;
    PFND3DDDI_SETDISPLAYMODECB               pfnSetDisplayModeCb;
    PFND3DDDI_PRESENTCB                      pfnPresentCb;
    PFND3DDDI_RENDERCB                       pfnRenderCb;
    PFND3DDDI_LOCKCB                         pfnLockCb;
    PFND3DDDI_UNLOCKCB                       pfnUnlockCb;
    PFND3DDDI_ESCAPECB                       pfnEscapeCb;
    PFND3DDDI_CREATEOVERLAYCB                pfnCreateOverlayCb;
    PFND3DDDI_UPDATEOVERLAYCB                pfnUpdateOverlayCb;
    PFND3DDDI_FLIPOVERLAYCB                  pfnFlipOverlayCb;
    PFND3DDDI_DESTROYOVERLAYCB               pfnDestroyOverlayCb;
    PFND3DDDI_CREATECONTEXTCB                pfnCreateContextCb;
    PFND3DDDI_DESTROYCONTEXTCB               pfnDestroyContextCb;
    PFND3DDDI_CREATESYNCHRONIZATIONOBJECTCB  pfnCreateSynchronizationObjectCb;
    PFND3DDDI_DESTROYSYNCHRONIZATIONOBJECTCB pfnDestroySynchronizationObjectCb;
    PFND3DDDI_WAITFORSYNCHRONIZATIONOBJECTCB pfnWaitForSynchronizationObjectCb;
    PFND3DDDI_SIGNALSYNCHRONIZATIONOBJECTCB  pfnSignalSynchronizationObjectCb;
    PFND3DDDI_SETASYNCCALLBACKSCB            pfnSetAsyncCallbacksCb;
    PFND3DDDI_SETDISPLAYPRIVATEDRIVERFORMATCB pfnSetDisplayPrivateDriverFormatCb;
} D3DDDI_DEVICECALLBACKS;

/* ===================================================================
 *  Core-layer callbacks — runtime → driver error/state notifications
 * =================================================================== */

typedef void (APIENTRY *PFND3D10DDI_SETERROR_CB)(D3D10DDI_HRTCORELAYER hRT, HRESULT hr);

typedef struct D3D10DDI_CORELAYER_DEVICECALLBACKS
{
    PFND3D10DDI_SETERROR_CB pfnSetErrorCb;
    /* Remaining callbacks are opaque to drivers that only call pfnSetErrorCb.
     * Reserve enough space so the struct is not smaller than the runtime's copy. */
    D3D10DDI_PFNTYPE pfnStateVsConstBufCb;
    D3D10DDI_PFNTYPE pfnStatePsSrvCb;
    D3D10DDI_PFNTYPE pfnStatePsShaderCb;
    D3D10DDI_PFNTYPE pfnStatePsSamplerCb;
    D3D10DDI_PFNTYPE pfnStateVsShaderCb;
    D3D10DDI_PFNTYPE pfnStatePsConstBufCb;
    D3D10DDI_PFNTYPE pfnStateIaInputLayoutCb;
    D3D10DDI_PFNTYPE pfnStateIaVertexBufCb;
    D3D10DDI_PFNTYPE pfnStateIaIndexBufCb;
    D3D10DDI_PFNTYPE pfnStateGsConstBufCb;
    D3D10DDI_PFNTYPE pfnStateGsShaderCb;
    D3D10DDI_PFNTYPE pfnStateIaPrimitiveTopologyCb;
    D3D10DDI_PFNTYPE pfnStateVsSrvCb;
    D3D10DDI_PFNTYPE pfnStateVsSamplerCb;
    D3D10DDI_PFNTYPE pfnStateGsSrvCb;
    D3D10DDI_PFNTYPE pfnStateGsSamplerCb;
    D3D10DDI_PFNTYPE pfnStateOmRenderTargetsCb;
    D3D10DDI_PFNTYPE pfnStateOmBlendStateCb;
    D3D10DDI_PFNTYPE pfnStateOmDepthStateCb;
    D3D10DDI_PFNTYPE pfnStateRsRastStateCb;
    D3D10DDI_PFNTYPE pfnStateSoTargetsCb;
    D3D10DDI_PFNTYPE pfnStateRsViewportsCb;
    D3D10DDI_PFNTYPE pfnStateRsScissorCb;
    D3D10DDI_PFNTYPE pfnDisableDeferredStagingResourceDestruction;
} D3D10DDI_CORELAYER_DEVICECALLBACKS;

/* ===================================================================
 *  Forward declarations for function table structs
 * =================================================================== */

struct D3D10DDI_DEVICEFUNCS;
struct D3D10_1DDI_DEVICEFUNCS;

/* ===================================================================
 *  D3D10DDI_DEVICEFUNCS — the massive device function pointer table
 *
 *  The driver fills in every non-NULL entry during CreateDevice.
 *  Order MUST match the Windows ABI.
 *
 *  All fields are typed as generic APIENTRY function pointers.
 *  The driver assigns its own correctly-typed functions; C/C++ allows
 *  implicit conversion between compatible function pointer types via
 *  assignment through a common generic type when using -fpermissive.
 * =================================================================== */

/* PFN_D3D10DDI_GENERIC is defined earlier in the file. */

struct D3D10DDI_DEVICEFUNCS
{
    D3D10DDI_PFNTYPE pfnDefaultConstantBufferUpdateSubresourceUP;
    D3D10DDI_PFNTYPE pfnVsSetConstantBuffers;
    D3D10DDI_PFNTYPE pfnPsSetShaderResources;
    D3D10DDI_PFNTYPE pfnPsSetShader;
    D3D10DDI_PFNTYPE pfnPsSetSamplers;
    D3D10DDI_PFNTYPE pfnVsSetShader;
    D3D10DDI_PFNTYPE pfnDrawIndexed;
    D3D10DDI_PFNTYPE pfnDraw;
    D3D10DDI_PFNTYPE pfnDynamicIABufferMapNoOverwrite;
    D3D10DDI_PFNTYPE pfnDynamicIABufferUnmap;
    D3D10DDI_PFNTYPE pfnDynamicConstantBufferMapDiscard;
    D3D10DDI_PFNTYPE pfnDynamicIABufferMapDiscard;
    D3D10DDI_PFNTYPE pfnDynamicConstantBufferUnmap;
    D3D10DDI_PFNTYPE pfnPsSetConstantBuffers;
    D3D10DDI_PFNTYPE pfnIaSetInputLayout;
    D3D10DDI_PFNTYPE pfnIaSetVertexBuffers;
    D3D10DDI_PFNTYPE pfnIaSetIndexBuffer;
    D3D10DDI_PFNTYPE pfnDrawIndexedInstanced;
    D3D10DDI_PFNTYPE pfnDrawInstanced;
    D3D10DDI_PFNTYPE pfnDynamicResourceMapDiscard;
    D3D10DDI_PFNTYPE pfnDynamicResourceUnmap;
    D3D10DDI_PFNTYPE pfnGsSetConstantBuffers;
    D3D10DDI_PFNTYPE pfnGsSetShader;
    D3D10DDI_PFNTYPE pfnIaSetTopology;
    D3D10DDI_PFNTYPE pfnStagingResourceMap;
    D3D10DDI_PFNTYPE pfnStagingResourceUnmap;
    D3D10DDI_PFNTYPE pfnVsSetShaderResources;
    D3D10DDI_PFNTYPE pfnVsSetSamplers;
    D3D10DDI_PFNTYPE pfnGsSetShaderResources;
    D3D10DDI_PFNTYPE pfnGsSetSamplers;
    D3D10DDI_PFNTYPE pfnSetRenderTargets;
    D3D10DDI_PFNTYPE pfnShaderResourceViewReadAfterWriteHazard;
    D3D10DDI_PFNTYPE pfnResourceReadAfterWriteHazard;
    D3D10DDI_PFNTYPE pfnSetBlendState;
    D3D10DDI_PFNTYPE pfnSetDepthStencilState;
    D3D10DDI_PFNTYPE pfnSetRasterizerState;
    D3D10DDI_PFNTYPE pfnQueryEnd;
    D3D10DDI_PFNTYPE pfnQueryBegin;
    D3D10DDI_PFNTYPE pfnResourceCopyRegion;
    D3D10DDI_PFNTYPE pfnResourceUpdateSubresourceUP;
    D3D10DDI_PFNTYPE pfnSoSetTargets;
    D3D10DDI_PFNTYPE pfnDrawAuto;
    D3D10DDI_PFNTYPE pfnSetViewports;
    D3D10DDI_PFNTYPE pfnSetScissorRects;
    D3D10DDI_PFNTYPE pfnClearRenderTargetView;
    D3D10DDI_PFNTYPE pfnClearDepthStencilView;
    D3D10DDI_PFNTYPE pfnSetPredication;
    D3D10DDI_PFNTYPE pfnQueryGetData;
    D3D10DDI_PFNTYPE pfnFlush;
    D3D10DDI_PFNTYPE pfnGenMips;
    D3D10DDI_PFNTYPE pfnResourceCopy;
    D3D10DDI_PFNTYPE pfnResourceResolveSubresource;
    D3D10DDI_PFNTYPE pfnResourceMap;
    D3D10DDI_PFNTYPE pfnResourceUnmap;
    D3D10DDI_PFNTYPE pfnResourceIsStagingBusy;
    D3D10DDI_PFNTYPE pfnRelocateDeviceFuncs;
    D3D10DDI_PFNTYPE pfnCalcPrivateResourceSize;
    D3D10DDI_PFNTYPE pfnCalcPrivateOpenedResourceSize;
    D3D10DDI_PFNTYPE pfnCreateResource;
    D3D10DDI_PFNTYPE pfnOpenResource;
    D3D10DDI_PFNTYPE pfnDestroyResource;
    D3D10DDI_PFNTYPE pfnCalcPrivateShaderResourceViewSize;
    D3D10DDI_PFNTYPE pfnCreateShaderResourceView;
    D3D10DDI_PFNTYPE pfnDestroyShaderResourceView;
    D3D10DDI_PFNTYPE pfnCalcPrivateRenderTargetViewSize;
    D3D10DDI_PFNTYPE pfnCreateRenderTargetView;
    D3D10DDI_PFNTYPE pfnDestroyRenderTargetView;
    D3D10DDI_PFNTYPE pfnCalcPrivateDepthStencilViewSize;
    D3D10DDI_PFNTYPE pfnCreateDepthStencilView;
    D3D10DDI_PFNTYPE pfnDestroyDepthStencilView;
    D3D10DDI_PFNTYPE pfnCalcPrivateElementLayoutSize;
    D3D10DDI_PFNTYPE pfnCreateElementLayout;
    D3D10DDI_PFNTYPE pfnDestroyElementLayout;
    D3D10DDI_PFNTYPE pfnCalcPrivateBlendStateSize;
    D3D10DDI_PFNTYPE pfnCreateBlendState;
    D3D10DDI_PFNTYPE pfnDestroyBlendState;
    D3D10DDI_PFNTYPE pfnCalcPrivateDepthStencilStateSize;
    D3D10DDI_PFNTYPE pfnCreateDepthStencilState;
    D3D10DDI_PFNTYPE pfnDestroyDepthStencilState;
    D3D10DDI_PFNTYPE pfnCalcPrivateRasterizerStateSize;
    D3D10DDI_PFNTYPE pfnCreateRasterizerState;
    D3D10DDI_PFNTYPE pfnDestroyRasterizerState;
    D3D10DDI_PFNTYPE pfnCalcPrivateShaderSize;
    D3D10DDI_PFNTYPE pfnCreateVertexShader;
    D3D10DDI_PFNTYPE pfnCreateGeometryShader;
    D3D10DDI_PFNTYPE pfnCreatePixelShader;
    D3D10DDI_PFNTYPE pfnCalcPrivateGeometryShaderWithStreamOutput;
    D3D10DDI_PFNTYPE pfnCreateGeometryShaderWithStreamOutput;
    D3D10DDI_PFNTYPE pfnDestroyShader;
    D3D10DDI_PFNTYPE pfnCalcPrivateSamplerSize;
    D3D10DDI_PFNTYPE pfnCreateSampler;
    D3D10DDI_PFNTYPE pfnDestroySampler;
    D3D10DDI_PFNTYPE pfnCalcPrivateQuerySize;
    D3D10DDI_PFNTYPE pfnCreateQuery;
    D3D10DDI_PFNTYPE pfnDestroyQuery;
    D3D10DDI_PFNTYPE pfnCheckFormatSupport;
    D3D10DDI_PFNTYPE pfnCheckMultisampleQualityLevels;
    D3D10DDI_PFNTYPE pfnCheckCounterInfo;
    D3D10DDI_PFNTYPE pfnCheckCounter;
    D3D10DDI_PFNTYPE pfnDestroyDevice;
    D3D10DDI_PFNTYPE pfnSetTextFilterSize;
};

/* ===================================================================
 *  D3D10_1DDI_DEVICEFUNCS — D3D10.1 extended function table
 *
 *  Inherits the base table layout then appends 10.1-specific entries.
 *  The driver fills both pDeviceFuncs (10.0) and p10_1DeviceFuncs (10.1).
 * =================================================================== */

struct D3D10_1DDI_DEVICEFUNCS
{
    /* Same order as D3D10DDI_DEVICEFUNCS base — all entries repeated */
    D3D10DDI_PFNTYPE pfnDefaultConstantBufferUpdateSubresourceUP;
    D3D10DDI_PFNTYPE pfnVsSetConstantBuffers;
    D3D10DDI_PFNTYPE pfnPsSetShaderResources;
    D3D10DDI_PFNTYPE pfnPsSetShader;
    D3D10DDI_PFNTYPE pfnPsSetSamplers;
    D3D10DDI_PFNTYPE pfnVsSetShader;
    D3D10DDI_PFNTYPE pfnDrawIndexed;
    D3D10DDI_PFNTYPE pfnDraw;
    D3D10DDI_PFNTYPE pfnDynamicIABufferMapNoOverwrite;
    D3D10DDI_PFNTYPE pfnDynamicIABufferUnmap;
    D3D10DDI_PFNTYPE pfnDynamicConstantBufferMapDiscard;
    D3D10DDI_PFNTYPE pfnDynamicIABufferMapDiscard;
    D3D10DDI_PFNTYPE pfnDynamicConstantBufferUnmap;
    D3D10DDI_PFNTYPE pfnPsSetConstantBuffers;
    D3D10DDI_PFNTYPE pfnIaSetInputLayout;
    D3D10DDI_PFNTYPE pfnIaSetVertexBuffers;
    D3D10DDI_PFNTYPE pfnIaSetIndexBuffer;
    D3D10DDI_PFNTYPE pfnDrawIndexedInstanced;
    D3D10DDI_PFNTYPE pfnDrawInstanced;
    D3D10DDI_PFNTYPE pfnDynamicResourceMapDiscard;
    D3D10DDI_PFNTYPE pfnDynamicResourceUnmap;
    D3D10DDI_PFNTYPE pfnGsSetConstantBuffers;
    D3D10DDI_PFNTYPE pfnGsSetShader;
    D3D10DDI_PFNTYPE pfnIaSetTopology;
    D3D10DDI_PFNTYPE pfnStagingResourceMap;
    D3D10DDI_PFNTYPE pfnStagingResourceUnmap;
    D3D10DDI_PFNTYPE pfnVsSetShaderResources;
    D3D10DDI_PFNTYPE pfnVsSetSamplers;
    D3D10DDI_PFNTYPE pfnGsSetShaderResources;
    D3D10DDI_PFNTYPE pfnGsSetSamplers;
    D3D10DDI_PFNTYPE pfnSetRenderTargets;
    D3D10DDI_PFNTYPE pfnShaderResourceViewReadAfterWriteHazard;
    D3D10DDI_PFNTYPE pfnResourceReadAfterWriteHazard;
    D3D10DDI_PFNTYPE pfnSetBlendState;
    D3D10DDI_PFNTYPE pfnSetDepthStencilState;
    D3D10DDI_PFNTYPE pfnSetRasterizerState;
    D3D10DDI_PFNTYPE pfnQueryEnd;
    D3D10DDI_PFNTYPE pfnQueryBegin;
    D3D10DDI_PFNTYPE pfnResourceCopyRegion;
    D3D10DDI_PFNTYPE pfnResourceUpdateSubresourceUP;
    D3D10DDI_PFNTYPE pfnSoSetTargets;
    D3D10DDI_PFNTYPE pfnDrawAuto;
    D3D10DDI_PFNTYPE pfnSetViewports;
    D3D10DDI_PFNTYPE pfnSetScissorRects;
    D3D10DDI_PFNTYPE pfnClearRenderTargetView;
    D3D10DDI_PFNTYPE pfnClearDepthStencilView;
    D3D10DDI_PFNTYPE pfnSetPredication;
    D3D10DDI_PFNTYPE pfnQueryGetData;
    D3D10DDI_PFNTYPE pfnFlush;
    D3D10DDI_PFNTYPE pfnGenMips;
    D3D10DDI_PFNTYPE pfnResourceCopy;
    D3D10DDI_PFNTYPE pfnResourceResolveSubresource;
    D3D10DDI_PFNTYPE pfnResourceMap;
    D3D10DDI_PFNTYPE pfnResourceUnmap;
    D3D10DDI_PFNTYPE pfnResourceIsStagingBusy;
    /* 10.1-specific replacements */
    D3D10DDI_PFNTYPE pfnRelocateDeviceFuncs;
    D3D10DDI_PFNTYPE pfnCalcPrivateResourceSize;
    D3D10DDI_PFNTYPE pfnCalcPrivateOpenedResourceSize;
    D3D10DDI_PFNTYPE pfnCreateResource;
    D3D10DDI_PFNTYPE pfnOpenResource;
    D3D10DDI_PFNTYPE pfnDestroyResource;
    D3D10DDI_PFNTYPE pfnCalcPrivateShaderResourceViewSize;
    D3D10DDI_PFNTYPE pfnCreateShaderResourceView;
    D3D10DDI_PFNTYPE pfnDestroyShaderResourceView;
    D3D10DDI_PFNTYPE pfnCalcPrivateRenderTargetViewSize;
    D3D10DDI_PFNTYPE pfnCreateRenderTargetView;
    D3D10DDI_PFNTYPE pfnDestroyRenderTargetView;
    D3D10DDI_PFNTYPE pfnCalcPrivateDepthStencilViewSize;
    D3D10DDI_PFNTYPE pfnCreateDepthStencilView;
    D3D10DDI_PFNTYPE pfnDestroyDepthStencilView;
    D3D10DDI_PFNTYPE pfnCalcPrivateElementLayoutSize;
    D3D10DDI_PFNTYPE pfnCreateElementLayout;
    D3D10DDI_PFNTYPE pfnDestroyElementLayout;
    D3D10DDI_PFNTYPE pfnCalcPrivateBlendStateSize;
    D3D10DDI_PFNTYPE pfnCreateBlendState;
    D3D10DDI_PFNTYPE pfnDestroyBlendState;
    D3D10DDI_PFNTYPE pfnCalcPrivateDepthStencilStateSize;
    D3D10DDI_PFNTYPE pfnCreateDepthStencilState;
    D3D10DDI_PFNTYPE pfnDestroyDepthStencilState;
    D3D10DDI_PFNTYPE pfnCalcPrivateRasterizerStateSize;
    D3D10DDI_PFNTYPE pfnCreateRasterizerState;
    D3D10DDI_PFNTYPE pfnDestroyRasterizerState;
    D3D10DDI_PFNTYPE pfnCalcPrivateShaderSize;
    D3D10DDI_PFNTYPE pfnCreateVertexShader;
    D3D10DDI_PFNTYPE pfnCreateGeometryShader;
    D3D10DDI_PFNTYPE pfnCreatePixelShader;
    D3D10DDI_PFNTYPE pfnCalcPrivateGeometryShaderWithStreamOutput;
    D3D10DDI_PFNTYPE pfnCreateGeometryShaderWithStreamOutput;
    D3D10DDI_PFNTYPE pfnDestroyShader;
    D3D10DDI_PFNTYPE pfnCalcPrivateSamplerSize;
    D3D10DDI_PFNTYPE pfnCreateSampler;
    D3D10DDI_PFNTYPE pfnDestroySampler;
    D3D10DDI_PFNTYPE pfnCalcPrivateQuerySize;
    D3D10DDI_PFNTYPE pfnCreateQuery;
    D3D10DDI_PFNTYPE pfnDestroyQuery;
    D3D10DDI_PFNTYPE pfnCheckFormatSupport;
    D3D10DDI_PFNTYPE pfnCheckMultisampleQualityLevels;
    D3D10DDI_PFNTYPE pfnCheckCounterInfo;
    D3D10DDI_PFNTYPE pfnCheckCounter;
    D3D10DDI_PFNTYPE pfnDestroyDevice;
    D3D10DDI_PFNTYPE pfnSetTextFilterSize;
    /* 10.1 additions */
    D3D10DDI_PFNTYPE pfnResourceConvert;
    D3D10DDI_PFNTYPE pfnResourceConvertRegion;
};

/* ===================================================================
 *  Adapter function tables
 * =================================================================== */

typedef HRESULT (APIENTRY *PFND3D10DDI_CLOSEADAPTER)(D3D10DDI_HADAPTER hAdapter);
typedef SIZE_T  (APIENTRY *PFND3D10DDI_CALCPRIVATEDEVICESIZE)(
    D3D10DDI_HADAPTER hAdapter, const D3D10DDIARG_CALCPRIVATEDEVICESIZE *pData);

struct D3D10DDIARG_CREATEDEVICE; /* forward */
typedef HRESULT (APIENTRY *PFND3D10DDI_CREATEDEVICE)(
    D3D10DDI_HADAPTER hAdapter, struct D3D10DDIARG_CREATEDEVICE *pCreateData);

typedef struct D3D10DDI_ADAPTERFUNCS
{
    PFND3D10DDI_CALCPRIVATEDEVICESIZE pfnCalcPrivateDeviceSize;
    PFND3D10DDI_CREATEDEVICE          pfnCreateDevice;
    PFND3D10DDI_CLOSEADAPTER          pfnCloseAdapter;
} D3D10DDI_ADAPTERFUNCS;

typedef HRESULT (APIENTRY *PFND3D10_2DDI_GETSUPPORTEDVERSIONS)(
    D3D10DDI_HADAPTER hAdapter, UINT32 *puEntries, UINT64 *pSupportedDDIInterfaceVersions);
typedef HRESULT (APIENTRY *PFND3D10_2DDI_GETCAPS)(
    D3D10DDI_HADAPTER hAdapter, const D3D10_2DDIARG_GETCAPS *pData);

typedef struct D3D10_2DDI_ADAPTERFUNCS
{
    PFND3D10_2DDI_GETSUPPORTEDVERSIONS pfnGetSupportedVersions;
    PFND3D10_2DDI_GETCAPS              pfnGetCaps;
} D3D10_2DDI_ADAPTERFUNCS;

/* ===================================================================
 *  OpenAdapter argument — the top-level entry point structure
 * =================================================================== */

typedef struct D3D10DDIARG_OPENADAPTER
{
    D3D10DDI_HRTDEVICE          hRTAdapter;        /* runtime handle */
    D3D10DDI_HADAPTER           hAdapter;           /* driver sets pDrvPrivate */
    UINT                        Interface;
    UINT                        Version;
    const void*                 pAdapterCallbacks;  /* D3DDDI_ADAPTERCALLBACKS* */
    D3D10DDI_ADAPTERFUNCS*      pAdapterFuncs;
    D3D10_2DDI_ADAPTERFUNCS*    pAdapterFuncs_2;
} D3D10DDIARG_OPENADAPTER;

/* ===================================================================
 *  CreateDevice argument
 * =================================================================== */

typedef struct D3D10DDIARG_CREATEDEVICE
{
    D3D10DDI_HRTDEVICE                    hRTDevice;
    UINT                                  Interface;
    UINT                                  Version;
    const D3DDDI_DEVICECALLBACKS*         pKTCallbacks;
    struct D3D10DDI_DEVICEFUNCS*           pDeviceFuncs;
    D3D10DDI_HDEVICE                      hDrvDevice;
    DXGI_DDI_BASE_ARGS                    DXGIBaseDDI;
    D3D10DDI_HRTCORELAYER                 hRTCoreLayer;
    const D3D10DDI_CORELAYER_DEVICECALLBACKS* pUMCallbacks;
    struct D3D10_1DDI_DEVICEFUNCS*         p10_1DeviceFuncs;
    UINT                                  Flags;
} D3D10DDIARG_CREATEDEVICE;

/* ===================================================================
 *  Entry point declarations — exported by UMD DLL
 * =================================================================== */

EXTERN_C HRESULT APIENTRY OpenAdapter10(D3D10DDIARG_OPENADAPTER *pOpenData);
EXTERN_C HRESULT APIENTRY OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *pOpenData);

#pragma warning(pop)

#ifdef __cplusplus
}
#endif

#endif /* _D3D10UMDDI_H_ */
