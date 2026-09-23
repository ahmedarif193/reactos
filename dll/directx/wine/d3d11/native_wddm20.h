/*
 * PROJECT:     ReactOS Direct3D 11 runtime
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     WDDM 2.0 build 9 device DDI declarations
 *
 * Function order and argument layouts follow the Windows SDK 10.0.16299
 * d3d10umddi.h contract with D3D11DDI_MINOR_HEADER_VERSION=9, without PSGP.
 * The older public SDK declarations remain available to legacy drivers.
 */
#ifndef _D3D11_NATIVE_WDDM20_H_
#define _D3D11_NATIVE_WDDM20_H_

#include <d3d10umddi.h>

struct D3DWDDM2_0DDI_DEVICEFUNCS;

typedef enum D3D10_SB_REGISTER_COMPONENT_TYPE
{
    D3D10_SB_REGISTER_COMPONENT_UNKNOWN = 0,
    D3D10_SB_REGISTER_COMPONENT_UINT32 = 1,
    D3D10_SB_REGISTER_COMPONENT_SINT32 = 2,
    D3D10_SB_REGISTER_COMPONENT_FLOAT32 = 3
} D3D10_SB_REGISTER_COMPONENT_TYPE;

typedef enum D3D11_SB_OPERAND_MIN_PRECISION
{
    D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT    = 0,

    D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16   = 1,
    D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_2_8  = 2,
    D3D11_SB_OPERAND_MIN_PRECISION_SINT_16    = 4,
    D3D11_SB_OPERAND_MIN_PRECISION_UINT_16    = 5,
} D3D11_SB_OPERAND_MIN_PRECISION;

typedef VOID ( APIENTRY* PFND3D11_1DDI_RESOURCEUPDATESUBRESOURCEUP )(
    D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, _In_opt_ CONST D3D10_DDI_BOX*, _In_ CONST VOID*, UINT, UINT, UINT CopyFlags );

typedef VOID ( APIENTRY* PFND3D11_1DDI_SETCONSTANTBUFFERS )(
    D3D10DDI_HDEVICE, _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot, _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers, _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE*,
    _In_reads_opt_(NumBuffers) CONST UINT* pFirstConstant,_In_reads_opt_(NumBuffers) CONST UINT* pNumConstants );

typedef VOID ( APIENTRY* PFND3D11_1DDI_RESOURCECOPYREGION )(
    D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, UINT, UINT, UINT, D3D10DDI_HRESOURCE, UINT, _In_opt_ CONST D3D10_DDI_BOX*, UINT CopyFlags );

typedef BOOL ( APIENTRY* PFND3DWDDM2_0DDI_FLUSH )(
    D3D10DDI_HDEVICE, UINT  contextType,
    UINT  FlushFlags );

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_RELOCATEDEVICEFUNCS )(
    D3D10DDI_HDEVICE, _In_ struct D3DWDDM2_0DDI_DEVICEFUNCS* );

typedef struct D3DWDDM2_0DDIARG_TEX2D_SHADERRESOURCEVIEW
{
    UINT     MostDetailedMip;
    UINT     FirstArraySlice;
    UINT     MipLevels;
    UINT     ArraySize;
    UINT PlaneSlice;
} D3DWDDM2_0DDIARG_TEX2D_SHADERRESOURCEVIEW;

typedef struct D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW
{
    D3D10DDI_HRESOURCE    hDrvResource;
    DXGI_FORMAT           Format;
    D3D10DDIRESOURCE_TYPE ResourceDimension;

    union
    {
        D3D10DDIARG_BUFFER_SHADERRESOURCEVIEW             Buffer;
        D3D10DDIARG_TEX1D_SHADERRESOURCEVIEW              Tex1D;
        D3DWDDM2_0DDIARG_TEX2D_SHADERRESOURCEVIEW         Tex2D;
        D3D10DDIARG_TEX3D_SHADERRESOURCEVIEW              Tex3D;
        D3D10_1DDIARG_TEXCUBE_SHADERRESOURCEVIEW          TexCube;
        D3D11DDIARG_BUFFEREX_SHADERRESOURCEVIEW           BufferEx;
    };
} D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW;

typedef SIZE_T ( APIENTRY* PFND3DWDDM2_0DDI_CALCPRIVATESHADERRESOURCEVIEWSIZE )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW* );

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_CREATESHADERRESOURCEVIEW )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW*, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRTSHADERRESOURCEVIEW );

typedef struct D3DWDDM2_0DDIARG_TEX2D_RENDERTARGETVIEW
{
    UINT     MipSlice;
    UINT     FirstArraySlice;
    UINT     ArraySize;
    UINT PlaneSlice;
} D3DWDDM2_0DDIARG_TEX2D_RENDERTARGETVIEW;

typedef struct D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW
{
    D3D10DDI_HRESOURCE    hDrvResource;
    DXGI_FORMAT           Format;
    D3D10DDIRESOURCE_TYPE ResourceDimension;

    union
    {
        D3D10DDIARG_BUFFER_RENDERTARGETVIEW  	Buffer;
        D3D10DDIARG_TEX1D_RENDERTARGETVIEW   	Tex1D;
        D3DWDDM2_0DDIARG_TEX2D_RENDERTARGETVIEW Tex2D;
        D3D10DDIARG_TEX3D_RENDERTARGETVIEW   	Tex3D;
        D3D10DDIARG_TEXCUBE_RENDERTARGETVIEW 	TexCube;
    };
} D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW;

typedef SIZE_T ( APIENTRY* PFND3DWDDM2_0DDI_CALCPRIVATERENDERTARGETVIEWSIZE )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW* );

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_CREATERENDERTARGETVIEW )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW*, D3D10DDI_HRENDERTARGETVIEW, D3D10DDI_HRTRENDERTARGETVIEW );

typedef enum D3D11_1_DDI_LOGIC_OP
{

    D3D11_1_DDI_LOGIC_OP_CLEAR = 0,
    D3D11_1_DDI_LOGIC_OP_SET,
    D3D11_1_DDI_LOGIC_OP_COPY,
    D3D11_1_DDI_LOGIC_OP_COPY_INVERTED,
    D3D11_1_DDI_LOGIC_OP_NOOP,
    D3D11_1_DDI_LOGIC_OP_INVERT,
    D3D11_1_DDI_LOGIC_OP_AND,
    D3D11_1_DDI_LOGIC_OP_NAND,
    D3D11_1_DDI_LOGIC_OP_OR,
    D3D11_1_DDI_LOGIC_OP_NOR,
    D3D11_1_DDI_LOGIC_OP_XOR,
    D3D11_1_DDI_LOGIC_OP_EQUIV,
    D3D11_1_DDI_LOGIC_OP_AND_REVERSE,
    D3D11_1_DDI_LOGIC_OP_AND_INVERTED,
    D3D11_1_DDI_LOGIC_OP_OR_REVERSE,
    D3D11_1_DDI_LOGIC_OP_OR_INVERTED,
} D3D11_1_DDI_LOGIC_OP;

typedef struct D3D11_1_DDI_RENDER_TARGET_BLEND_DESC
{
    BOOL BlendEnable;
    BOOL LogicOpEnable;
    D3D10_DDI_BLEND SrcBlend;
    D3D10_DDI_BLEND DestBlend;
    D3D10_DDI_BLEND_OP BlendOp;
    D3D10_DDI_BLEND SrcBlendAlpha;
    D3D10_DDI_BLEND DestBlendAlpha;
    D3D10_DDI_BLEND_OP BlendOpAlpha;
    D3D11_1_DDI_LOGIC_OP LogicOp;
    UINT8 RenderTargetWriteMask;
} D3D11_1_DDI_RENDER_TARGET_BLEND_DESC;

typedef struct D3D11_1_DDI_BLEND_DESC
{
    BOOL AlphaToCoverageEnable;
    BOOL IndependentBlendEnable;
    D3D11_1_DDI_RENDER_TARGET_BLEND_DESC RenderTarget[D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT];
} D3D11_1_DDI_BLEND_DESC;

typedef SIZE_T ( APIENTRY* PFND3D11_1DDI_CALCPRIVATEBLENDSTATESIZE )(
    D3D10DDI_HDEVICE, _In_ CONST D3D11_1_DDI_BLEND_DESC* );

typedef VOID ( APIENTRY* PFND3D11_1DDI_CREATEBLENDSTATE )(
    D3D10DDI_HDEVICE, _In_ CONST D3D11_1_DDI_BLEND_DESC*, D3D10DDI_HBLENDSTATE, D3D10DDI_HRTBLENDSTATE );

typedef enum D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_MODE
{
    D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_OFF = 0,
    D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_ON  = 1,
} D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_MODE;

typedef struct D3DWDDM2_0DDI_RASTERIZER_DESC
{
    D3D10_DDI_FILL_MODE FillMode;
    D3D10_DDI_CULL_MODE CullMode;
    BOOL FrontCounterClockwise;
    INT DepthBias;
    FLOAT DepthBiasClamp;
    FLOAT SlopeScaledDepthBias;
    BOOL DepthClipEnable;
    BOOL ScissorEnable;
    BOOL MultisampleEnable;
    BOOL AntialiasedLineEnable;
    UINT ForcedSampleCount;
    D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_MODE ConservativeRasterizationMode;
} D3DWDDM2_0DDI_RASTERIZER_DESC;

typedef SIZE_T ( APIENTRY* PFND3DWDDM2_0DDI_CALCPRIVATERASTERIZERSTATESIZE )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDI_RASTERIZER_DESC* );

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_CREATERASTERIZERSTATE )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDI_RASTERIZER_DESC*, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE );

typedef struct D3D11_1DDIARG_SIGNATURE_ENTRY
{
    D3D10_SB_NAME SystemValue;
    UINT Register;
    BYTE Mask;
    D3D10_SB_REGISTER_COMPONENT_TYPE RegisterComponentType;
    D3D11_SB_OPERAND_MIN_PRECISION   MinPrecision;
} D3D11_1DDIARG_SIGNATURE_ENTRY;

typedef struct D3D11_1DDIARG_SIGNATURE_ENTRY2
{
    D3D10_SB_NAME SystemValue;
    UINT Register;
    BYTE Mask;
    BYTE Stream;

    D3D10_SB_REGISTER_COMPONENT_TYPE RegisterComponentType;
    D3D11_SB_OPERAND_MIN_PRECISION   MinPrecision;
} D3D11_1DDIARG_SIGNATURE_ENTRY2;

typedef struct D3D11_1DDIARG_STAGE_IO_SIGNATURES
{

    union
    {
        D3D11_1DDIARG_SIGNATURE_ENTRY*  pInputSignatureDeprecated;
        D3D11_1DDIARG_SIGNATURE_ENTRY2* pInputSignature;
    };
    UINT                                NumInputSignatureEntries;
    union
    {
        D3D11_1DDIARG_SIGNATURE_ENTRY*  pOutputSignatureDeprecated;
        D3D11_1DDIARG_SIGNATURE_ENTRY2* pOutputSignature;
    };
    UINT                                NumOutputSignatureEntries;
} D3D11_1DDIARG_STAGE_IO_SIGNATURES;

typedef SIZE_T ( APIENTRY* PFND3D11_1DDI_CALCPRIVATESHADERSIZE )(
    D3D10DDI_HDEVICE, _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode, _In_ CONST D3D11_1DDIARG_STAGE_IO_SIGNATURES* );

typedef VOID ( APIENTRY* PFND3D11_1DDI_CREATEVERTEXSHADER )(
    D3D10DDI_HDEVICE, _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, _In_ CONST D3D11_1DDIARG_STAGE_IO_SIGNATURES* );

typedef VOID ( APIENTRY* PFND3D11_1DDI_CREATEGEOMETRYSHADER )(
    D3D10DDI_HDEVICE, _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, _In_ CONST D3D11_1DDIARG_STAGE_IO_SIGNATURES* );

typedef VOID ( APIENTRY* PFND3D11_1DDI_CREATEPIXELSHADER )(
    D3D10DDI_HDEVICE, _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, _In_ CONST D3D11_1DDIARG_STAGE_IO_SIGNATURES* );

typedef SIZE_T ( APIENTRY* PFND3D11_1DDI_CALCPRIVATEGEOMETRYSHADERWITHSTREAMOUTPUT )(
    D3D10DDI_HDEVICE, _In_ CONST D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT*, _In_ CONST D3D11_1DDIARG_STAGE_IO_SIGNATURES* );

typedef VOID ( APIENTRY* PFND3D11_1DDI_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT )(
    D3D10DDI_HDEVICE, _In_ CONST D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT*, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, _In_ CONST D3D11_1DDIARG_STAGE_IO_SIGNATURES* );

typedef struct D3DWDDM2_0DDIARG_CREATEQUERY
{
    D3D10DDI_QUERY Query;
    UINT MiscFlags;
    UINT ContextType;
} D3DWDDM2_0DDIARG_CREATEQUERY;

typedef SIZE_T ( APIENTRY* PFND3DWDDM2_0DDI_CALCPRIVATEQUERYSIZE )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATEQUERY* );

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_CREATEQUERY )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATEQUERY*, D3D10DDI_HQUERY, D3D10DDI_HRTQUERY );

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_CHECKMULTISAMPLEQUALITYLEVELS )(
    D3D10DDI_HDEVICE hDevice,
    DXGI_FORMAT Format,
    UINT SampleCount,
    UINT Flags,
    _Out_ UINT* pNumQualityLevels
);

typedef struct D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES
{

    union
    {
        D3D11_1DDIARG_SIGNATURE_ENTRY*  pInputSignatureDeprecated;
        D3D11_1DDIARG_SIGNATURE_ENTRY2* pInputSignature;
    };
    UINT                                NumInputSignatureEntries;
    union
    {
        D3D11_1DDIARG_SIGNATURE_ENTRY*  pOutputSignatureDeprecated;
        D3D11_1DDIARG_SIGNATURE_ENTRY2* pOutputSignature;
    };
    UINT                                NumOutputSignatureEntries;
    union
    {
        D3D11_1DDIARG_SIGNATURE_ENTRY*  pPatchConstantSignatureDeprecated;
        D3D11_1DDIARG_SIGNATURE_ENTRY2* pPatchConstantSignature;
    };
    UINT                                NumPatchConstantSignatureEntries;
} D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES;

typedef VOID ( APIENTRY* PFND3D11_1DDI_CREATEHULLSHADER )(
    D3D10DDI_HDEVICE, _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, _In_ CONST D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES* );

typedef VOID ( APIENTRY* PFND3D11_1DDI_CREATEDOMAINSHADER )(
    D3D10DDI_HDEVICE, _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, _In_ CONST D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES* );

typedef SIZE_T ( APIENTRY* PFND3D11_1DDI_CALCPRIVATETESSELLATIONSHADERSIZE )(
    D3D10DDI_HDEVICE, _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode, _In_ CONST D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES* );

typedef struct D3DWDDM2_0DDIARG_TEX2D_UNORDEREDACCESSVIEW
{
    UINT     MipSlice;
    UINT     FirstArraySlice;
    UINT     ArraySize;
    UINT PlaneSlice;
} D3DWDDM2_0DDIARG_TEX2D_UNORDEREDACCESSVIEW;

typedef struct D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW
{
    D3D10DDI_HRESOURCE    hDrvResource;
    DXGI_FORMAT           Format;
    D3D10DDIRESOURCE_TYPE ResourceDimension;

    union
    {
        D3D11DDIARG_BUFFER_UNORDEREDACCESSVIEW    	Buffer;
        D3D11DDIARG_TEX1D_UNORDEREDACCESSVIEW     	Tex1D;
        D3DWDDM2_0DDIARG_TEX2D_UNORDEREDACCESSVIEW	Tex2D;
        D3D11DDIARG_TEX3D_UNORDEREDACCESSVIEW     	Tex3D;
    };
} D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW;

typedef SIZE_T ( APIENTRY* PFND3DWDDM2_0DDI_CALCPRIVATEUNORDEREDACCESSVIEWSIZE )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW* );

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_CREATEUNORDEREDACCESSVIEW )(
    D3D10DDI_HDEVICE, _In_ CONST D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW*, D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW );

typedef VOID ( APIENTRY* PFND3D11_1DDI_DISCARD )(
    D3D10DDI_HDEVICE, D3D11DDI_HANDLETYPE HandleType, VOID* hResourceOrView, _In_reads_opt_(NumRects) CONST D3D10_DDI_RECT*, UINT NumRects );

typedef VOID ( APIENTRY* PFND3D11_1DDI_ASSIGNDEBUGBINARY )(
    D3D10DDI_HDEVICE, D3D10DDI_HSHADER,
    UINT uBinarySize, _In_reads_bytes_(uBinarySize) CONST VOID* pBinary);

typedef VOID ( APIENTRY* PFND3D11_1DDI_CHECKDIRECTFLIPSUPPORT )(
    D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, D3D10DDI_HRESOURCE, UINT CheckDirectFlipFlags, _Out_ BOOL* pSupported );

typedef VOID ( APIENTRY* PFND3D11_1DDI_CLEARVIEW )(
    D3D10DDI_HDEVICE hDevice, D3D11DDI_HANDLETYPE viewType, VOID* hView, CONST FLOAT Color[4], _In_reads_opt_(NumRects) CONST D3D10_DDI_RECT* pRect, UINT NumRects );

typedef struct D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE
{

    UINT X;
    UINT Y;
    UINT Z;
    UINT Subresource;

} D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE;

typedef struct D3DWDDM1_3DDI_TILE_REGION_SIZE
{
    UINT NumTiles;
    BOOL bUseBox;

    UINT Width;
    UINT16 Height;
    UINT16 Depth;
} D3DWDDM1_3DDI_TILE_REGION_SIZE;

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_UPDATETILEMAPPINGS )(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hTiledResource,
    UINT NumTiledResourceRegions,
    _In_reads_(NumTiledResourceRegions) const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE* pTiledResourceRegionStartCoords,
    _In_reads_opt_(NumTiledResourceRegions) const D3DWDDM1_3DDI_TILE_REGION_SIZE* pTiledResourceRegionSizes,
    D3D10DDI_HRESOURCE hTilePool,
    UINT NumRanges,
    _In_reads_opt_(NumRanges) const UINT* pRangeFlags,
    _In_reads_opt_(NumRanges) const UINT* pTilePoolStartOffsets,
    _In_reads_opt_(NumRanges) const UINT* pRangeTileCounts,
    UINT Flags
);

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_COPYTILEMAPPINGS )(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDestTiledResource,
    _In_ const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE* pDestRegionStartCoord,
    D3D10DDI_HRESOURCE hSourceTiledResource,
    _In_ const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE* pSourceRegionStartCoord,
    _In_ const D3DWDDM1_3DDI_TILE_REGION_SIZE* pTileRegionSize,
    UINT Flags
);

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_COPYTILES )(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hTiledResource,
    _In_ const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE* pTileRegionStartCoord,
    _In_ const D3DWDDM1_3DDI_TILE_REGION_SIZE* pTileRegionSize,
    D3D10DDI_HRESOURCE hBuffer,
    UINT64 BufferStartOffsetInBytes,
    UINT Flags
);

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_UPDATETILES )(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDestTiledResource,
    _In_ const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE* pDestTileRegionStartCoord,
    _In_ const D3DWDDM1_3DDI_TILE_REGION_SIZE* pDestTileRegionSize,
    _In_ const VOID* pSourceTileData,
    UINT Flags
);

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_TILEDRESOURCEBARRIER )(
    D3D10DDI_HDEVICE hDevice,
    D3D11DDI_HANDLETYPE TiledResourceAccessBeforeBarrierHandleType,
    _In_opt_ VOID* hTiledResourceAccessBeforeBarrier,
    D3D11DDI_HANDLETYPE TiledResourceAccessAfterBarrierHandleType,
    _In_opt_ VOID* hTiledResourceAccessAfterBarrier
);

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_GETMIPPACKING )(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hTiledResource,
    _Out_ UINT* pNumPackedMips,
    _Out_ UINT* pNumTilesForPackedMips
);

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_RESIZETILEPOOL )(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hTilePool,
    UINT64 NewSizeInBytes
);

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_SETMARKER )(
    D3D10DDI_HDEVICE hDevice );

typedef enum D3DWDDM1_3DDI_MARKER_TYPE
{
    D3DWDDM1_3DDI_MARKER_TYPE_NONE,
    D3DWDDM1_3DDI_MARKER_TYPE_PROFILE,
} D3DWDDM1_3DDI_MARKER_TYPE;

typedef VOID ( APIENTRY* PFND3DWDDM1_3DDI_SETMARKERMODE )(
    D3D10DDI_HDEVICE hDevice, D3DWDDM1_3DDI_MARKER_TYPE Type,  UINT Flags );

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_SETHARDWAREPROTECTION )(
    D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hResource, BOOL Protected );

typedef struct D3DWDDM2_0DDI_SUBRESOURCE_PRESWIZZLE_OFFSETS
{
   UINT16 RowByteOffset;
   UINT16 ColumnOffset;
   UINT16 DepthOffset;
} D3DWDDM2_0DDI_SUBRESOURCE_PRESWIZZLE_OFFSETS;

typedef struct D3DWDDM2_0DDI_SUBRESOURCE_LAYOUT
{
    UINT64 BaseOffset;
    D3DWDDM2_0DDI_SUBRESOURCE_PRESWIZZLE_OFFSETS PreswizzleOffsets;
    UINT64 RowPitch;
    UINT64 DepthPitch;
} D3DWDDM2_0DDI_SUBRESOURCE_LAYOUT;

typedef VOID ( APIENTRY* PFND3DWDDM2_0DDI_GETRESOURCELAYOUT )(
    D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT SubresourceCount,
    _Out_ D3DKMT_HANDLE *, _Out_ D3DWDDM2_0DDI_TEXTURE_LAYOUT *,
    _Out_ UINT *pMipLevelSwizzleTransition,
    _Out_writes_opt_(SubresourceCount) D3DWDDM2_0DDI_SUBRESOURCE_LAYOUT * );

typedef HRESULT ( APIENTRY* PFND3DWDDM2_0DDI_RETRIEVE_SHADER_COMMENT )(
    D3D10DDI_HDEVICE, D3D10DDI_HSHADER, _Out_writes_z_(*CharacterCountIncludingNullTerminator) WCHAR * pBuffer, _Inout_ SIZE_T * CharacterCountIncludingNullTerminator );

typedef void ( APIENTRY* PFND3DWDDM2_0DDI_SETHARDWAREPROTECTIONSTATE )(
    D3D10DDI_HDEVICE, BOOL HwProtectionEnable );

typedef struct D3DWDDM2_0DDI_DEVICEFUNCS
{

    PFND3D11_1DDI_RESOURCEUPDATESUBRESOURCEUP               pfnDefaultConstantBufferUpdateSubresourceUP;
    PFND3D11_1DDI_SETCONSTANTBUFFERS                        pfnVsSetConstantBuffers;
    PFND3D10DDI_SETSHADERRESOURCES                          pfnPsSetShaderResources;
    PFND3D10DDI_SETSHADER                                   pfnPsSetShader;
    PFND3D10DDI_SETSAMPLERS                                 pfnPsSetSamplers;
    PFND3D10DDI_SETSHADER                                   pfnVsSetShader;
    PFND3D10DDI_DRAWINDEXED                                 pfnDrawIndexed;
    PFND3D10DDI_DRAW                                        pfnDraw;
    PFND3D10DDI_RESOURCEMAP                                 pfnDynamicIABufferMapNoOverwrite;
    PFND3D10DDI_RESOURCEUNMAP                               pfnDynamicIABufferUnmap;
    PFND3D10DDI_RESOURCEMAP                                 pfnDynamicConstantBufferMapDiscard;
    PFND3D10DDI_RESOURCEMAP                                 pfnDynamicIABufferMapDiscard;
    PFND3D10DDI_RESOURCEUNMAP                               pfnDynamicConstantBufferUnmap;
    PFND3D11_1DDI_SETCONSTANTBUFFERS                        pfnPsSetConstantBuffers;
    PFND3D10DDI_SETINPUTLAYOUT                              pfnIaSetInputLayout;
    PFND3D10DDI_IA_SETVERTEXBUFFERS                         pfnIaSetVertexBuffers;
    PFND3D10DDI_IA_SETINDEXBUFFER                           pfnIaSetIndexBuffer;

    PFND3D10DDI_DRAWINDEXEDINSTANCED                        pfnDrawIndexedInstanced;
    PFND3D10DDI_DRAWINSTANCED                               pfnDrawInstanced;
    PFND3D10DDI_RESOURCEMAP                                 pfnDynamicResourceMapDiscard;
    PFND3D10DDI_RESOURCEUNMAP                               pfnDynamicResourceUnmap;
    PFND3D11_1DDI_SETCONSTANTBUFFERS                        pfnGsSetConstantBuffers;
    PFND3D10DDI_SETSHADER                                   pfnGsSetShader;
    PFND3D10DDI_IA_SETTOPOLOGY                              pfnIaSetTopology;
    PFND3D10DDI_RESOURCEMAP                                 pfnStagingResourceMap;
    PFND3D10DDI_RESOURCEUNMAP                               pfnStagingResourceUnmap;
    PFND3D10DDI_SETSHADERRESOURCES                          pfnVsSetShaderResources;
    PFND3D10DDI_SETSAMPLERS                                 pfnVsSetSamplers;
    PFND3D10DDI_SETSHADERRESOURCES                          pfnGsSetShaderResources;
    PFND3D10DDI_SETSAMPLERS                                 pfnGsSetSamplers;
    PFND3D11DDI_SETRENDERTARGETS                            pfnSetRenderTargets;
    PFND3D10DDI_SHADERRESOURCEVIEWREADAFTERWRITEHAZARD      pfnShaderResourceViewReadAfterWriteHazard;
    PFND3D10DDI_RESOURCEREADAFTERWRITEHAZARD                pfnResourceReadAfterWriteHazard;
    PFND3D10DDI_SETBLENDSTATE                               pfnSetBlendState;
    PFND3D10DDI_SETDEPTHSTENCILSTATE                        pfnSetDepthStencilState;
    PFND3D10DDI_SETRASTERIZERSTATE                          pfnSetRasterizerState;
    PFND3D10DDI_QUERYEND                                    pfnQueryEnd;
    PFND3D10DDI_QUERYBEGIN                                  pfnQueryBegin;
    PFND3D11_1DDI_RESOURCECOPYREGION                        pfnResourceCopyRegion;
    PFND3D11_1DDI_RESOURCEUPDATESUBRESOURCEUP               pfnResourceUpdateSubresourceUP;
    PFND3D10DDI_SO_SETTARGETS                               pfnSoSetTargets;
    PFND3D10DDI_DRAWAUTO                                    pfnDrawAuto;
    PFND3D10DDI_SETVIEWPORTS                                pfnSetViewports;
    PFND3D10DDI_SETSCISSORRECTS                             pfnSetScissorRects;
    PFND3D10DDI_CLEARRENDERTARGETVIEW                       pfnClearRenderTargetView;
    PFND3D10DDI_CLEARDEPTHSTENCILVIEW                       pfnClearDepthStencilView;
    PFND3D10DDI_SETPREDICATION                              pfnSetPredication;
    PFND3D10DDI_QUERYGETDATA                                pfnQueryGetData;
    PFND3DWDDM2_0DDI_FLUSH                                  pfnFlush;
    PFND3D10DDI_GENMIPS                                     pfnGenMips;
    PFND3D10DDI_RESOURCECOPY                                pfnResourceCopy;
    PFND3D10DDI_RESOURCERESOLVESUBRESOURCE                  pfnResourceResolveSubresource;

    PFND3D10DDI_RESOURCEMAP                                 pfnResourceMap;
    PFND3D10DDI_RESOURCEUNMAP                               pfnResourceUnmap;
    PFND3D10DDI_RESOURCEISSTAGINGBUSY                       pfnResourceIsStagingBusy;
    PFND3DWDDM2_0DDI_RELOCATEDEVICEFUNCS                    pfnRelocateDeviceFuncs;
    PFND3D11DDI_CALCPRIVATERESOURCESIZE                     pfnCalcPrivateResourceSize;
    PFND3D10DDI_CALCPRIVATEOPENEDRESOURCESIZE               pfnCalcPrivateOpenedResourceSize;
    PFND3D11DDI_CREATERESOURCE                              pfnCreateResource;
    PFND3D10DDI_OPENRESOURCE                                pfnOpenResource;
    PFND3D10DDI_DESTROYRESOURCE                             pfnDestroyResource;
    PFND3DWDDM2_0DDI_CALCPRIVATESHADERRESOURCEVIEWSIZE      pfnCalcPrivateShaderResourceViewSize;
    PFND3DWDDM2_0DDI_CREATESHADERRESOURCEVIEW               pfnCreateShaderResourceView;
    PFND3D10DDI_DESTROYSHADERRESOURCEVIEW                   pfnDestroyShaderResourceView;
    PFND3DWDDM2_0DDI_CALCPRIVATERENDERTARGETVIEWSIZE        pfnCalcPrivateRenderTargetViewSize;
    PFND3DWDDM2_0DDI_CREATERENDERTARGETVIEW                 pfnCreateRenderTargetView;
    PFND3D10DDI_DESTROYRENDERTARGETVIEW                     pfnDestroyRenderTargetView;
    PFND3D11DDI_CALCPRIVATEDEPTHSTENCILVIEWSIZE             pfnCalcPrivateDepthStencilViewSize;
    PFND3D11DDI_CREATEDEPTHSTENCILVIEW                      pfnCreateDepthStencilView;
    PFND3D10DDI_DESTROYDEPTHSTENCILVIEW                     pfnDestroyDepthStencilView;
    PFND3D10DDI_CALCPRIVATEELEMENTLAYOUTSIZE                pfnCalcPrivateElementLayoutSize;
    PFND3D10DDI_CREATEELEMENTLAYOUT                         pfnCreateElementLayout;
    PFND3D10DDI_DESTROYELEMENTLAYOUT                        pfnDestroyElementLayout;
    PFND3D11_1DDI_CALCPRIVATEBLENDSTATESIZE                 pfnCalcPrivateBlendStateSize;
    PFND3D11_1DDI_CREATEBLENDSTATE                          pfnCreateBlendState;
    PFND3D10DDI_DESTROYBLENDSTATE                           pfnDestroyBlendState;
    PFND3D10DDI_CALCPRIVATEDEPTHSTENCILSTATESIZE            pfnCalcPrivateDepthStencilStateSize;
    PFND3D10DDI_CREATEDEPTHSTENCILSTATE                     pfnCreateDepthStencilState;
    PFND3D10DDI_DESTROYDEPTHSTENCILSTATE                    pfnDestroyDepthStencilState;
    PFND3DWDDM2_0DDI_CALCPRIVATERASTERIZERSTATESIZE         pfnCalcPrivateRasterizerStateSize;
    PFND3DWDDM2_0DDI_CREATERASTERIZERSTATE                  pfnCreateRasterizerState;
    PFND3D10DDI_DESTROYRASTERIZERSTATE                      pfnDestroyRasterizerState;
    PFND3D11_1DDI_CALCPRIVATESHADERSIZE                     pfnCalcPrivateShaderSize;
    PFND3D11_1DDI_CREATEVERTEXSHADER                        pfnCreateVertexShader;
    PFND3D11_1DDI_CREATEGEOMETRYSHADER                      pfnCreateGeometryShader;
    PFND3D11_1DDI_CREATEPIXELSHADER                         pfnCreatePixelShader;
    PFND3D11_1DDI_CALCPRIVATEGEOMETRYSHADERWITHSTREAMOUTPUT pfnCalcPrivateGeometryShaderWithStreamOutput;
    PFND3D11_1DDI_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT      pfnCreateGeometryShaderWithStreamOutput;
    PFND3D10DDI_DESTROYSHADER                               pfnDestroyShader;
    PFND3D10DDI_CALCPRIVATESAMPLERSIZE                      pfnCalcPrivateSamplerSize;
    PFND3D10DDI_CREATESAMPLER                               pfnCreateSampler;
    PFND3D10DDI_DESTROYSAMPLER                              pfnDestroySampler;
    PFND3DWDDM2_0DDI_CALCPRIVATEQUERYSIZE                   pfnCalcPrivateQuerySize;
    PFND3DWDDM2_0DDI_CREATEQUERY                            pfnCreateQuery;
    PFND3D10DDI_DESTROYQUERY                                pfnDestroyQuery;

    PFND3D10DDI_CHECKFORMATSUPPORT                          pfnCheckFormatSupport;
    PFND3DWDDM1_3DDI_CHECKMULTISAMPLEQUALITYLEVELS          pfnCheckMultisampleQualityLevels;
    PFND3D10DDI_CHECKCOUNTERINFO                            pfnCheckCounterInfo;
    PFND3D10DDI_CHECKCOUNTER                                pfnCheckCounter;

    PFND3D10DDI_DESTROYDEVICE                               pfnDestroyDevice;
    PFND3D10DDI_SETTEXTFILTERSIZE                           pfnSetTextFilterSize;

    PFND3D10DDI_RESOURCECOPY                                pfnResourceConvert;
    PFND3D11_1DDI_RESOURCECOPYREGION                        pfnResourceConvertRegion;

    PFND3D11DDI_DRAWINDEXEDINSTANCEDINDIRECT                pfnDrawIndexedInstancedIndirect;
    PFND3D11DDI_DRAWINSTANCEDINDIRECT                       pfnDrawInstancedIndirect;
    PFND3D11DDI_COMMANDLISTEXECUTE                          pfnCommandListExecute;
    PFND3D10DDI_SETSHADERRESOURCES                          pfnHsSetShaderResources;
    PFND3D10DDI_SETSHADER                                   pfnHsSetShader;
    PFND3D10DDI_SETSAMPLERS                                 pfnHsSetSamplers;
    PFND3D11_1DDI_SETCONSTANTBUFFERS                        pfnHsSetConstantBuffers;
    PFND3D10DDI_SETSHADERRESOURCES                          pfnDsSetShaderResources;
    PFND3D10DDI_SETSHADER                                   pfnDsSetShader;
    PFND3D10DDI_SETSAMPLERS                                 pfnDsSetSamplers;
    PFND3D11_1DDI_SETCONSTANTBUFFERS                        pfnDsSetConstantBuffers;
    PFND3D11_1DDI_CREATEHULLSHADER                          pfnCreateHullShader;
    PFND3D11_1DDI_CREATEDOMAINSHADER                        pfnCreateDomainShader;
    PFND3D11DDI_CHECKDEFERREDCONTEXTHANDLESIZES             pfnCheckDeferredContextHandleSizes;
    PFND3D11DDI_CALCDEFERREDCONTEXTHANDLESIZE               pfnCalcDeferredContextHandleSize;
    PFND3D11DDI_CALCPRIVATEDEFERREDCONTEXTSIZE              pfnCalcPrivateDeferredContextSize;
    PFND3D11DDI_CREATEDEFERREDCONTEXT                       pfnCreateDeferredContext;
    PFND3D11DDI_ABANDONCOMMANDLIST                          pfnAbandonCommandList;
    PFND3D11DDI_CALCPRIVATECOMMANDLISTSIZE                  pfnCalcPrivateCommandListSize;
    PFND3D11DDI_CREATECOMMANDLIST                           pfnCreateCommandList;
    PFND3D11DDI_DESTROYCOMMANDLIST                          pfnDestroyCommandList;
    PFND3D11_1DDI_CALCPRIVATETESSELLATIONSHADERSIZE         pfnCalcPrivateTessellationShaderSize;
    PFND3D11DDI_SETSHADER_WITH_IFACES                       pfnPsSetShaderWithIfaces;
    PFND3D11DDI_SETSHADER_WITH_IFACES                       pfnVsSetShaderWithIfaces;
    PFND3D11DDI_SETSHADER_WITH_IFACES                       pfnGsSetShaderWithIfaces;
    PFND3D11DDI_SETSHADER_WITH_IFACES                       pfnHsSetShaderWithIfaces;
    PFND3D11DDI_SETSHADER_WITH_IFACES                       pfnDsSetShaderWithIfaces;
    PFND3D11DDI_SETSHADER_WITH_IFACES                       pfnCsSetShaderWithIfaces;
    PFND3D11DDI_CREATECOMPUTESHADER                         pfnCreateComputeShader;
    PFND3D10DDI_SETSHADER                                   pfnCsSetShader;
    PFND3D10DDI_SETSHADERRESOURCES                          pfnCsSetShaderResources;
    PFND3D10DDI_SETSAMPLERS                                 pfnCsSetSamplers;
    PFND3D11_1DDI_SETCONSTANTBUFFERS                        pfnCsSetConstantBuffers;
    PFND3DWDDM2_0DDI_CALCPRIVATEUNORDEREDACCESSVIEWSIZE     pfnCalcPrivateUnorderedAccessViewSize;
    PFND3DWDDM2_0DDI_CREATEUNORDEREDACCESSVIEW              pfnCreateUnorderedAccessView;
    PFND3D11DDI_DESTROYUNORDEREDACCESSVIEW                  pfnDestroyUnorderedAccessView;
    PFND3D11DDI_CLEARUNORDEREDACCESSVIEWUINT                pfnClearUnorderedAccessViewUint;
    PFND3D11DDI_CLEARUNORDEREDACCESSVIEWFLOAT               pfnClearUnorderedAccessViewFloat;
    PFND3D11DDI_SETUNORDEREDACCESSVIEWS                     pfnCsSetUnorderedAccessViews;
    PFND3D11DDI_DISPATCH                                    pfnDispatch;
    PFND3D11DDI_DISPATCHINDIRECT                            pfnDispatchIndirect;
    PFND3D11DDI_SETRESOURCEMINLOD                           pfnSetResourceMinLOD;
    PFND3D11DDI_COPYSTRUCTURECOUNT                          pfnCopyStructureCount;
    PFND3D11DDI_RECYCLECOMMANDLIST                          pfnRecycleCommandList;
    PFND3D11DDI_RECYCLECREATECOMMANDLIST                    pfnRecycleCreateCommandList;
    PFND3D11DDI_RECYCLECREATEDEFERREDCONTEXT                pfnRecycleCreateDeferredContext;
    PFND3D11DDI_DESTROYCOMMANDLIST                          pfnRecycleDestroyCommandList;

    PFND3D11_1DDI_DISCARD                                   pfnDiscard;
    PFND3D11_1DDI_ASSIGNDEBUGBINARY                         pfnAssignDebugBinary;
    PFND3D10DDI_RESOURCEMAP                                 pfnDynamicConstantBufferMapNoOverwrite;
    PFND3D11_1DDI_CHECKDIRECTFLIPSUPPORT                    pfnCheckDirectFlipSupport;
    PFND3D11_1DDI_CLEARVIEW                                 pfnClearView;

    PFND3DWDDM1_3DDI_UPDATETILEMAPPINGS                     pfnUpdateTileMappings;
    PFND3DWDDM1_3DDI_COPYTILEMAPPINGS                       pfnCopyTileMappings;
    PFND3DWDDM1_3DDI_COPYTILES                              pfnCopyTiles;
    PFND3DWDDM1_3DDI_UPDATETILES                            pfnUpdateTiles;
    PFND3DWDDM1_3DDI_TILEDRESOURCEBARRIER                   pfnTiledResourceBarrier;
    PFND3DWDDM1_3DDI_GETMIPPACKING                          pfnGetMipPacking;
    PFND3DWDDM1_3DDI_RESIZETILEPOOL                         pfnResizeTilePool;
    PFND3DWDDM1_3DDI_SETMARKER                              pfnSetMarker;
    PFND3DWDDM1_3DDI_SETMARKERMODE                          pfnSetMarkerMode;

    PFND3DWDDM2_0DDI_SETHARDWAREPROTECTION                  pfnSetHardwareProtection;
    PFND3DWDDM2_0DDI_GETRESOURCELAYOUT                      pfnGetResourceLayout;
    PFND3DWDDM2_0DDI_RETRIEVE_SHADER_COMMENT                pfnRetrieveShaderComment;
    PFND3DWDDM2_0DDI_SETHARDWAREPROTECTIONSTATE             pfnSetHardwareProtectionState;
} D3DWDDM2_0DDI_DEVICEFUNCS;

typedef struct D3DWDDM2_0DDICB_CREATECONTEXT
{
    UINT                        NodeOrdinal;
    UINT                        EngineAffinity;
    D3DDDI_CREATECONTEXTFLAGS   Flags;
    UINT                        ContextTypeFlags;
    VOID*                       pPrivateDriverData;
    UINT                        PrivateDriverDataSize;
    HANDLE                      hContext;
    VOID*                       pCommandBuffer;
    UINT                        CommandBufferSize;
    D3DDDI_ALLOCATIONLIST*      pAllocationList;
    UINT                        AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST*   pPatchLocationList;
    UINT                        PatchLocationListSize;
    D3DGPU_VIRTUAL_ADDRESS      CommandBuffer;
} D3DWDDM2_0DDICB_CREATECONTEXT;

typedef _Check_return_ HRESULT(APIENTRY CALLBACK *PFND3DWDDM2_0DDI_CREATECONTEXT_CB)(
    _In_ D3D10DDI_HRTCORELAYER hDevice,
    _Inout_ D3DWDDM2_0DDICB_CREATECONTEXT*
    );

typedef struct D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL
{
    UINT                        NodeOrdinal;
    UINT                        EngineAffinity;
    D3DDDI_CREATECONTEXTFLAGS   Flags;
    UINT                        ContextTypeFlags;
    VOID*                       pPrivateDriverData;
    UINT                        PrivateDriverDataSize;
    HANDLE                      hContext;
} D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL;

typedef _Check_return_ HRESULT(APIENTRY CALLBACK *PFND3DWDDM2_0DDI_CREATECONTEXTVIRTUAL_CB)(
    _In_ D3D10DDI_HRTCORELAYER hDevice,
    _Inout_ D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL*
    );

typedef struct D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS
{
    PFND3D10DDI_SETERROR_CB pfnSetErrorCb;
    PFND3D10DDI_STATE_VS_CONSTBUF_CB pfnStateVsConstBufCb;
    PFND3D10DDI_STATE_PS_SRV_CB pfnStatePsSrvCb;
    PFND3D10DDI_STATE_PS_SHADER_CB pfnStatePsShaderCb;
    PFND3D10DDI_STATE_PS_SAMPLER_CB pfnStatePsSamplerCb;
    PFND3D10DDI_STATE_VS_SHADER_CB pfnStateVsShaderCb;
    PFND3D10DDI_STATE_PS_CONSTBUF_CB pfnStatePsConstBufCb;
    PFND3D10DDI_STATE_IA_INPUTLAYOUT_CB pfnStateIaInputLayoutCb;
    PFND3D10DDI_STATE_IA_VERTEXBUF_CB pfnStateIaVertexBufCb;
    PFND3D10DDI_STATE_IA_INDEXBUF_CB pfnStateIaIndexBufCb;
    PFND3D10DDI_STATE_GS_CONSTBUF_CB pfnStateGsConstBufCb;
    PFND3D10DDI_STATE_GS_SHADER_CB pfnStateGsShaderCb;
    PFND3D10DDI_STATE_IA_PRIMITIVE_TOPOLOGY_CB pfnStateIaPrimitiveTopologyCb;
    PFND3D10DDI_STATE_VS_SRV_CB pfnStateVsSrvCb;
    PFND3D10DDI_STATE_VS_SAMPLER_CB pfnStateVsSamplerCb;
    PFND3D10DDI_STATE_GS_SRV_CB pfnStateGsSrvCb;
    PFND3D10DDI_STATE_GS_SAMPLER_CB pfnStateGsSamplerCb;
    PFND3D10DDI_STATE_OM_RENDERTARGETS_CB pfnStateOmRenderTargetsCb;
    PFND3D10DDI_STATE_OM_BLENDSTATE_CB pfnStateOmBlendStateCb;
    PFND3D10DDI_STATE_OM_DEPTHSTATE_CB pfnStateOmDepthStateCb;
    PFND3D10DDI_STATE_RS_RASTSTATE_CB pfnStateRsRastStateCb;
    PFND3D10DDI_STATE_SO_TARGETS_CB pfnStateSoTargetsCb;
    PFND3D10DDI_STATE_RS_VIEWPORTS_CB pfnStateRsViewportsCb;
    PFND3D10DDI_STATE_RS_SCISSOR_CB pfnStateRsScissorCb;
    PFND3D10DDI_DISABLE_DEFERRED_STAGING_RESOURCE_DESTRUCTION_CB pfnDisableDeferredStagingResourceDestruction;
    PFND3D10DDI_STATE_TEXTFILTERSIZE_CB pfnStateTextFilterSizeCb;
    PFND3D11DDI_STATE_HS_SRV_CB pfnStateHsSrvCb;
    PFND3D11DDI_STATE_HS_SHADER_CB pfnStateHsShaderCb;
    PFND3D11DDI_STATE_HS_SAMPLER_CB pfnStateHsSamplerCb;
    PFND3D11DDI_STATE_HS_CONSTBUF_CB pfnStateHsConstBufCb;
    PFND3D11DDI_STATE_DS_SRV_CB pfnStateDsSrvCb;
    PFND3D11DDI_STATE_DS_SHADER_CB pfnStateDsShaderCb;
    PFND3D11DDI_STATE_DS_SAMPLER_CB pfnStateDsSamplerCb;
    PFND3D11DDI_STATE_DS_CONSTBUF_CB pfnStateDsConstBufCb;
    PFND3D11DDI_PERFORM_AMORTIZED_PROCESSING_CB pfnPerformAmortizedProcessingCb;
    PFND3D11DDI_STATE_CS_SRV_CB              pfnStateCsSrvCb;
    PFND3D11DDI_STATE_CS_UAV_CB              pfnStateCsUavCb;
    PFND3D11DDI_STATE_CS_SHADER_CB           pfnStateCsShaderCb;
    PFND3D11DDI_STATE_CS_SAMPLER_CB          pfnStateCsSamplerCb;
    PFND3D11DDI_STATE_CS_CONSTBUF_CB         pfnStateCsConstBufCb;
    PFND3DWDDM2_0DDI_CREATECONTEXT_CB        pfnCreateContextCb;
    PFND3DWDDM2_0DDI_CREATECONTEXTVIRTUAL_CB pfnCreateContextVirtualCb;
} D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS;


#define NATIVE_WDDM20_INTERFACE 0x000b0020u
#define NATIVE_WDDM20_BUILD 9u

typedef struct DXGI_DDI_ARG_BLT1 DXGI_DDI_ARG_BLT1;
typedef struct DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT;
typedef struct DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT;
typedef struct DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS;
typedef struct DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS;
typedef struct DXGI_DDI_ARG_OFFERRESOURCES DXGI_DDI_ARG_OFFERRESOURCES;
typedef struct DXGI_DDI_ARG_PRESENT1 DXGI_DDI_ARG_PRESENT1;
typedef struct DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY;
typedef struct DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1 DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1;
typedef struct DXGI_DDI_ARG_RECLAIMRESOURCES DXGI_DDI_ARG_RECLAIMRESOURCES;
typedef struct DXGI_DDI_ARG_TRIMRESIDENCYSET DXGI_DDI_ARG_TRIMRESIDENCYSET;

typedef struct DXGI1_4_DDI_BASE_FUNCTIONS
{
    HRESULT ( APIENTRY  * pfnPresent )               (DXGI_DDI_ARG_PRESENT*);
    HRESULT ( APIENTRY  * pfnGetGammaCaps )          (DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS*);
    HRESULT ( APIENTRY  * pfnSetDisplayMode )        (DXGI_DDI_ARG_SETDISPLAYMODE*);
    HRESULT ( APIENTRY  * pfnSetResourcePriority )   (DXGI_DDI_ARG_SETRESOURCEPRIORITY*);
    HRESULT ( APIENTRY  * pfnQueryResourceResidency )(DXGI_DDI_ARG_QUERYRESOURCERESIDENCY*);
    HRESULT ( APIENTRY  * pfnRotateResourceIdentities )(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES*);
    HRESULT ( APIENTRY  * pfnBlt                    )(DXGI_DDI_ARG_BLT*);
    HRESULT ( APIENTRY  * pfnResolveSharedResource ) (DXGI_DDI_ARG_RESOLVESHAREDRESOURCE*);
    HRESULT ( APIENTRY  * pfnBlt1 )                  (DXGI_DDI_ARG_BLT1*);
    HRESULT ( APIENTRY  * pfnOfferResources )        (DXGI_DDI_ARG_OFFERRESOURCES*);
    HRESULT ( APIENTRY  * pfnReclaimResources )      (DXGI_DDI_ARG_RECLAIMRESOURCES*);
    HRESULT ( APIENTRY  * pfnGetMultiplaneOverlayCaps )        (DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS*);
    HRESULT ( APIENTRY  * pfnGetMultiplaneOverlayGroupCaps )   (DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS*);
    HRESULT ( APIENTRY  * pfnReserved1 )                       (void*);
    HRESULT ( APIENTRY  * pfnPresentMultiplaneOverlay )        (DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY*);
    HRESULT ( APIENTRY  * pfnReserved2 )                       (void*);
    HRESULT ( APIENTRY  * pfnPresent1 )                        (DXGI_DDI_ARG_PRESENT1*);
    HRESULT ( APIENTRY  * pfnCheckPresentDurationSupport )     (DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT*);
    HRESULT ( APIENTRY  * pfnTrimResidencySet )                (DXGI_DDI_ARG_TRIMRESIDENCYSET*);
    HRESULT ( APIENTRY  * pfnCheckMultiplaneOverlayColorSpaceSupport )     (DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT*);
    HRESULT ( APIENTRY  * pfnPresentMultiplaneOverlay1 )        (DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1*);
}DXGI1_4_DDI_BASE_FUNCTIONS;

static_assert(sizeof(D3DWDDM2_0DDI_DEVICEFUNCS) == 168 * sizeof(void *), "WDDM 2.0 function table");
static_assert(sizeof(D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS) == 42 * sizeof(void *), "WDDM 2.0 core callbacks");
static_assert(sizeof(DXGI1_4_DDI_BASE_FUNCTIONS) == 21 * sizeof(void *), "DXGI 1.4 function table");
static_assert(sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2) == 20, "WDDM shader signature entry");
static_assert(offsetof(D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS, pfnCreateContextCb) == 40 * sizeof(void *), "Core callback extension");
#ifdef _WIN64
static_assert(sizeof(D3D11DDIARG_CREATERESOURCE) == 80, "WDDM resource arguments");
static_assert(offsetof(D3D10DDIARG_CREATEDEVICE, ppfnRetrieveSubObject) == 80, "Subobject callback layout");
static_assert(sizeof(D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL) == 40, "Virtual context arguments");
#endif

#endif
