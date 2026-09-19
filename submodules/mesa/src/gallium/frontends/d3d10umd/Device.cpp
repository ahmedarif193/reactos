/**************************************************************************
 *
 * Copyright 2012-2021 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 **************************************************************************/

/*
 * Device.cpp --
 *    Functions that provide the 3D device functionality.
 */


#include "Draw.h"
#include "DxgiFns.h"
#include "InputAssembly.h"
#include "OutputMerger.h"
#include "Query.h"
#include "Rasterizer.h"
#include "Resource.h"
#include "Shader.h"
#include "State.h"
#include "Format.h"

#include "Debug.h"

#include "util/u_sampler.h"
#include "util/u_framebuffer.h"

#include <stddef.h>

EXTERN_C struct pipe_screen *
d3d10_create_screen(void *adapter, void *device, const void *callbacks);

EXTERN_C void *
d3d10_get_present_context(struct pipe_screen *screen);


static void APIENTRY DestroyDevice(D3D10DDI_HDEVICE hDevice);
static void APIENTRY RelocateDeviceFuncs(D3D10DDI_HDEVICE hDevice,
                                __in struct D3D10DDI_DEVICEFUNCS *pDeviceFunctions);
#if SUPPORT_D3D10_1
static void APIENTRY RelocateDeviceFuncs1(D3D10DDI_HDEVICE hDevice,
                                __in struct D3D10_1DDI_DEVICEFUNCS *pDeviceFunctions);
#endif
#if SUPPORT_D3D11
static void APIENTRY RelocateDeviceFuncs11(D3D10DDI_HDEVICE hDevice,
                                __in struct D3D11DDI_DEVICEFUNCS *pDeviceFunctions);
static void APIENTRY SetRenderTargets11(
   D3D10DDI_HDEVICE hDevice,
   const D3D10DDI_HRENDERTARGETVIEW *phRenderTargetView,
   UINT RTargets, UINT ClearTargets,
   D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
   const D3D11DDI_HUNORDEREDACCESSVIEW *phUnorderedAccessView,
   const UINT *pUAVInitialCounts,
   UINT UAVStartSlot, UINT NumUAVs, UINT UAVRangeStart, UINT UAVRangeSize);
static SIZE_T APIENTRY CalcPrivateResourceSize11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATERESOURCE *pCreateResource);
static void APIENTRY CreateResource11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATERESOURCE *pCreateResource,
   D3D10DDI_HRESOURCE hResource, D3D10DDI_HRTRESOURCE hRTResource);
static SIZE_T APIENTRY CalcPrivateShaderResourceViewSize11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATESHADERRESOURCEVIEW *pCreateShaderResourceView);
static void APIENTRY CreateShaderResourceView11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATESHADERRESOURCEVIEW *pCreateShaderResourceView,
   D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
   D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView);
static SIZE_T APIENTRY CalcPrivateDepthStencilViewSize11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *pCreateDepthStencilView);
static void APIENTRY CreateDepthStencilView11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *pCreateDepthStencilView,
   D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
   D3D10DDI_HRTDEPTHSTENCILVIEW hRTDepthStencilView);
static SIZE_T APIENTRY CalcPrivateGeometryShaderWithStreamOutput11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *pCreateData,
   const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures);
static void APIENTRY CreateGeometryShaderWithStreamOutput11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *pCreateData,
   D3D10DDI_HSHADER hShader, D3D10DDI_HRTSHADER hRTShader,
   const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures);
#endif
static void APIENTRY Flush(D3D10DDI_HDEVICE hDevice);
static void APIENTRY CheckFormatSupport(D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format,
                               __out UINT *pFormatCaps);
static void APIENTRY CheckMultisampleQualityLevels(D3D10DDI_HDEVICE hDevice,
                                          DXGI_FORMAT Format,
                                          UINT SampleCount,
                                          __out UINT *pNumQualityLevels);
static void APIENTRY SetTextFilterSize(D3D10DDI_HDEVICE hDevice, UINT Width, UINT Height);


#if SUPPORT_D3D11
static bool
ResourceIsD3D10Compatible(const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
   const UINT unsupported_misc =
      D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS |
      D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS |
      D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED |
      D3D11_DDI_RESOURCE_MISC_RESOURCE_CLAMP;

   return pCreateResource->ResourceDimension != D3D11DDIRESOURCE_BUFFEREX &&
          !(pCreateResource->BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS) &&
          !(pCreateResource->MiscFlags & unsupported_misc) &&
          pCreateResource->ByteStride == 0;
}


static D3D10DDIARG_CREATERESOURCE
ResourceArgs10(const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
   D3D10DDIARG_CREATERESOURCE args = {};
   args.pMipInfoList = pCreateResource->pMipInfoList;
   args.pInitialDataUP = pCreateResource->pInitialDataUP;
   args.ResourceDimension = pCreateResource->ResourceDimension;
   args.Usage = pCreateResource->Usage;
   args.BindFlags = pCreateResource->BindFlags;
   args.MapFlags = pCreateResource->MapFlags;
   args.MiscFlags = pCreateResource->MiscFlags;
   args.Format = pCreateResource->Format;
   args.SampleDesc = pCreateResource->SampleDesc;
   args.MipLevels = pCreateResource->MipLevels;
   args.ArraySize = pCreateResource->ArraySize;
   args.pPrimaryDesc = pCreateResource->pPrimaryDesc;
   return args;
}


static D3D10_1DDIARG_CREATESHADERRESOURCEVIEW
ShaderResourceViewArgs10_1(
   const D3D11DDIARG_CREATESHADERRESOURCEVIEW *pCreateShaderResourceView)
{
   D3D10_1DDIARG_CREATESHADERRESOURCEVIEW args = {};
   args.hDrvResource = pCreateShaderResourceView->hDrvResource;
   args.Format = pCreateShaderResourceView->Format;
   args.ResourceDimension = pCreateShaderResourceView->ResourceDimension;

   switch (args.ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER:
      args.Buffer = pCreateShaderResourceView->Buffer;
      break;
   case D3D10DDIRESOURCE_TEXTURE1D:
      args.Tex1D = pCreateShaderResourceView->Tex1D;
      break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      args.Tex2D = pCreateShaderResourceView->Tex2D;
      break;
   case D3D10DDIRESOURCE_TEXTURE3D:
      args.Tex3D = pCreateShaderResourceView->Tex3D;
      break;
   case D3D10DDIRESOURCE_TEXTURECUBE:
      args.TexCube = pCreateShaderResourceView->TexCube;
      break;
   default:
      break;
   }

   return args;
}


static D3D10DDIARG_CREATEDEPTHSTENCILVIEW
DepthStencilViewArgs10(
   const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *pCreateDepthStencilView)
{
   D3D10DDIARG_CREATEDEPTHSTENCILVIEW args = {};
   args.hDrvResource = pCreateDepthStencilView->hDrvResource;
   args.Format = pCreateDepthStencilView->Format;
   args.ResourceDimension = pCreateDepthStencilView->ResourceDimension;

   switch (args.ResourceDimension) {
   case D3D10DDIRESOURCE_TEXTURE1D:
      args.Tex1D = pCreateDepthStencilView->Tex1D;
      break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      args.Tex2D = pCreateDepthStencilView->Tex2D;
      break;
   case D3D10DDIRESOURCE_TEXTURECUBE:
      args.TexCube = pCreateDepthStencilView->TexCube;
      break;
   default:
      break;
   }

   return args;
}


static bool
GeometryShaderWithStreamOutputIsD3D10Compatible(
   const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *pCreateData)
{
   if ((pCreateData->NumEntries && !pCreateData->pOutputStreamDecl) ||
       (pCreateData->NumStrides && !pCreateData->BufferStridesInBytes))
      return false;

   if (pCreateData->NumStrides > 1 || pCreateData->RasterizedStream != 0)
      return false;

   for (UINT i = 0; i < pCreateData->NumEntries; ++i) {
      if (pCreateData->pOutputStreamDecl[i].Stream != 0)
         return false;
   }

   return true;
}
#endif


/*
 * ----------------------------------------------------------------------
 *
 * CalcPrivateDeviceSize --
 *
 *    The CalcPrivateDeviceSize function determines the size of a memory
 *    region that the user-mode display driver requires from the Microsoft
 *    Direct3D runtime to store frequently-accessed data.
 *
 * ----------------------------------------------------------------------
 */

SIZE_T APIENTRY
CalcPrivateDeviceSize(D3D10DDI_HADAPTER hAdapter,                          // IN
                      __in const D3D10DDIARG_CALCPRIVATEDEVICESIZE *pData) // IN
{
   return sizeof(Device);
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateDevice --
 *
 *    The CreateDevice function creates a graphics context that is
 *    referenced in subsequent calls.
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
CreateDevice(D3D10DDI_HADAPTER hAdapter,                 // IN
             __in D3D10DDIARG_CREATEDEVICE *pCreateData) // IN
{
   LOG_ENTRYPOINT();

   bool isD3D11 = false;

   if (0) {
      DebugPrintf("hAdapter = %p\n", hAdapter);
      DebugPrintf("pKTCallbacks = %p\n", pCreateData->pKTCallbacks);
      DebugPrintf("p10_1DeviceFuncs = %p\n", pCreateData->p10_1DeviceFuncs);
      DebugPrintf("hDrvDevice = %p\n", pCreateData->hDrvDevice);
      DebugPrintf("DXGIBaseDDI = %p\n", pCreateData->DXGIBaseDDI);
      DebugPrintf("hRTCoreLayer = %p\n", pCreateData->hRTCoreLayer);
      DebugPrintf("pUMCallbacks = %p\n", pCreateData->pUMCallbacks);
   }

   switch (pCreateData->Interface) {
   case D3D10_0_DDI_INTERFACE_VERSION:
   case D3D10_0_x_DDI_INTERFACE_VERSION:
   case D3D10_0_7_DDI_INTERFACE_VERSION:
#if SUPPORT_D3D10_1
   case D3D10_1_DDI_INTERFACE_VERSION:
   case D3D10_1_x_DDI_INTERFACE_VERSION:
   case D3D10_1_7_DDI_INTERFACE_VERSION:
#endif
      break;
#if SUPPORT_D3D11
   case D3D11_0_DDI_INTERFACE_VERSION:
   case D3D11_0_7_DDI_INTERFACE_VERSION:
      isD3D11 = true;
      break;
#endif
   default:
      DebugPrintf("%s: unsupported interface version 0x%08x\n",
                  __func__, pCreateData->Interface);
      return E_FAIL;
   }

   Adapter *pAdapter = CastAdapter(hAdapter);

   Device *pDevice = CastDevice(pCreateData->hDrvDevice);
   memset(pDevice, 0, sizeof *pDevice);

   pDevice->hRTCoreLayer = pCreateData->hRTCoreLayer;
   pDevice->hDevice = (HANDLE)pCreateData->hRTDevice.handle;
   pDevice->KTCallbacks = *pCreateData->pKTCallbacks;
   pDevice->UMCallbacks = *pCreateData->pUMCallbacks;
   pDevice->pDXGIBaseCallbacks = pCreateData->DXGIBaseDDI.pDXGIBaseCallbacks;

   struct pipe_screen *screen =
      d3d10_create_screen(pAdapter->hAdapter, pDevice->hDevice,
                          &pDevice->KTCallbacks);
   if (!screen)
      return E_FAIL;
   pDevice->screen = screen;
   pDevice->hContext = d3d10_get_present_context(screen);

   struct pipe_context *pipe = screen->context_create(screen, NULL, 0);
   if (!pipe) {
      screen->destroy(screen);
      pDevice->screen = NULL;
      return E_FAIL;
   }
   pDevice->pipe = pipe;
   pDevice->cso = cso_create_context(pipe, CSO_NO_VBUF);
   if (!pDevice->cso) {
      pipe->destroy(pipe);
      pDevice->pipe = NULL;
      screen->destroy(screen);
      pDevice->screen = NULL;
      return E_OUTOFMEMORY;
   }

   pDevice->empty_vs = CreateEmptyShader(pDevice, MESA_SHADER_VERTEX);
   pDevice->empty_fs = CreateEmptyShader(pDevice, MESA_SHADER_FRAGMENT);

   pipe->bind_vs_state(pipe, pDevice->empty_vs);
   pipe->bind_fs_state(pipe, pDevice->empty_fs);

   const FLOAT default_blend_factor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
   const D3D10DDI_HBLENDSTATE default_blend = {};
   const D3D10DDI_HDEPTHSTENCILSTATE default_depth_stencil = {};
   const D3D10DDI_HRASTERIZERSTATE default_rasterizer = {};

   SetBlendState(pCreateData->hDrvDevice, default_blend,
                 default_blend_factor, ~0u);
   SetDepthStencilState(pCreateData->hDrvDevice, default_depth_stencil, 0);
   SetRasterizerState(pCreateData->hDrvDevice, default_rasterizer);

   if (!pDevice->default_blend_state ||
       !pDevice->default_depth_stencil_state ||
       !pDevice->default_rasterizer_state) {
      DestroyDevice(pCreateData->hDrvDevice);
      return E_OUTOFMEMORY;
   }

   pDevice->max_dual_source_render_targets =
         screen->caps.max_dual_source_render_targets;

   pDevice->draw_so_target = NULL;

   if (0) {
      DebugPrintf("pDevice = %p\n", pDevice);
   }

   st_debug_parse();

   /*
    * Fill in the D3D10 DDI functions
    */
#if SUPPORT_D3D11
   static_assert(sizeof(D3D10DDI_DEVICEFUNCS) ==
                 offsetof(D3D11DDI_DEVICEFUNCS, pfnResourceConvert),
                 "D3D11 core dispatch must retain the D3D10 table prefix");
   if (isD3D11)
      memset(pCreateData->p11DeviceFuncs, 0, sizeof(*pCreateData->p11DeviceFuncs));
#endif
   D3D10DDI_DEVICEFUNCS *pDeviceFuncs = isD3D11
      ? reinterpret_cast<D3D10DDI_DEVICEFUNCS *>(pCreateData->p11DeviceFuncs)
      : pCreateData->pDeviceFuncs;
   pDeviceFuncs->pfnDefaultConstantBufferUpdateSubresourceUP = ResourceUpdateSubResourceUP;
   pDeviceFuncs->pfnVsSetConstantBuffers = VsSetConstantBuffers;
   pDeviceFuncs->pfnPsSetShaderResources = PsSetShaderResources;
   pDeviceFuncs->pfnPsSetShader = PsSetShader;
   pDeviceFuncs->pfnPsSetSamplers = PsSetSamplers;
   pDeviceFuncs->pfnVsSetShader = VsSetShader;
   pDeviceFuncs->pfnDrawIndexed = DrawIndexed;
   pDeviceFuncs->pfnDraw = Draw;
   pDeviceFuncs->pfnDynamicIABufferMapNoOverwrite = ResourceMap;
   pDeviceFuncs->pfnDynamicIABufferUnmap = ResourceUnmap;
   pDeviceFuncs->pfnDynamicConstantBufferMapDiscard = ResourceMap;
   pDeviceFuncs->pfnDynamicIABufferMapDiscard = ResourceMap;
   pDeviceFuncs->pfnDynamicConstantBufferUnmap = ResourceUnmap;
   pDeviceFuncs->pfnPsSetConstantBuffers = PsSetConstantBuffers;
   pDeviceFuncs->pfnIaSetInputLayout = IaSetInputLayout;
   pDeviceFuncs->pfnIaSetVertexBuffers = IaSetVertexBuffers;
   pDeviceFuncs->pfnIaSetIndexBuffer = IaSetIndexBuffer;
   pDeviceFuncs->pfnDrawIndexedInstanced = DrawIndexedInstanced;
   pDeviceFuncs->pfnDrawInstanced = DrawInstanced;
   pDeviceFuncs->pfnDynamicResourceMapDiscard = ResourceMap;
   pDeviceFuncs->pfnDynamicResourceUnmap = ResourceUnmap;
   pDeviceFuncs->pfnGsSetConstantBuffers = GsSetConstantBuffers;
   pDeviceFuncs->pfnGsSetShader = GsSetShader;
   pDeviceFuncs->pfnIaSetTopology = IaSetTopology;
   pDeviceFuncs->pfnStagingResourceMap = ResourceMap;
   pDeviceFuncs->pfnStagingResourceUnmap = ResourceUnmap;
   pDeviceFuncs->pfnVsSetShaderResources = VsSetShaderResources;
   pDeviceFuncs->pfnVsSetSamplers = VsSetSamplers;
   pDeviceFuncs->pfnGsSetShaderResources = GsSetShaderResources;
   pDeviceFuncs->pfnGsSetSamplers = GsSetSamplers;
   pDeviceFuncs->pfnSetRenderTargets = SetRenderTargets;
   pDeviceFuncs->pfnShaderResourceViewReadAfterWriteHazard = ShaderResourceViewReadAfterWriteHazard;
   pDeviceFuncs->pfnResourceReadAfterWriteHazard = ResourceReadAfterWriteHazard;
   pDeviceFuncs->pfnSetBlendState = SetBlendState;
   pDeviceFuncs->pfnSetDepthStencilState = SetDepthStencilState;
   pDeviceFuncs->pfnSetRasterizerState = SetRasterizerState;
   pDeviceFuncs->pfnQueryEnd = QueryEnd;
   pDeviceFuncs->pfnQueryBegin = QueryBegin;
   pDeviceFuncs->pfnResourceCopyRegion = ResourceCopyRegion;
   pDeviceFuncs->pfnResourceUpdateSubresourceUP = ResourceUpdateSubResourceUP;
   pDeviceFuncs->pfnSoSetTargets = SoSetTargets;
   pDeviceFuncs->pfnDrawAuto = DrawAuto;
   pDeviceFuncs->pfnSetViewports = SetViewports;
   pDeviceFuncs->pfnSetScissorRects = SetScissorRects;
   pDeviceFuncs->pfnClearRenderTargetView = ClearRenderTargetView;
   pDeviceFuncs->pfnClearDepthStencilView = ClearDepthStencilView;
   pDeviceFuncs->pfnSetPredication = SetPredication;
   pDeviceFuncs->pfnQueryGetData = QueryGetData;
   pDeviceFuncs->pfnFlush = Flush;
   pDeviceFuncs->pfnGenMips = GenMips;
   pDeviceFuncs->pfnResourceCopy = ResourceCopy;
   pDeviceFuncs->pfnResourceResolveSubresource = ResourceResolveSubResource;
   pDeviceFuncs->pfnResourceMap = ResourceMap;
   pDeviceFuncs->pfnResourceUnmap = ResourceUnmap;
   pDeviceFuncs->pfnResourceIsStagingBusy = ResourceIsStagingBusy;
   pDeviceFuncs->pfnRelocateDeviceFuncs = RelocateDeviceFuncs;
   pDeviceFuncs->pfnCalcPrivateResourceSize = CalcPrivateResourceSize;
   pDeviceFuncs->pfnCalcPrivateOpenedResourceSize = CalcPrivateOpenedResourceSize;
   pDeviceFuncs->pfnCreateResource = CreateResource;
   pDeviceFuncs->pfnOpenResource = OpenResource;
   pDeviceFuncs->pfnDestroyResource = DestroyResource;
   pDeviceFuncs->pfnCalcPrivateShaderResourceViewSize = CalcPrivateShaderResourceViewSize;
   pDeviceFuncs->pfnCreateShaderResourceView = CreateShaderResourceView;
   pDeviceFuncs->pfnDestroyShaderResourceView = DestroyShaderResourceView;
   pDeviceFuncs->pfnCalcPrivateRenderTargetViewSize = CalcPrivateRenderTargetViewSize;
   pDeviceFuncs->pfnCreateRenderTargetView = CreateRenderTargetView;
   pDeviceFuncs->pfnDestroyRenderTargetView = DestroyRenderTargetView;
   pDeviceFuncs->pfnCalcPrivateDepthStencilViewSize = CalcPrivateDepthStencilViewSize;
   pDeviceFuncs->pfnCreateDepthStencilView = CreateDepthStencilView;
   pDeviceFuncs->pfnDestroyDepthStencilView = DestroyDepthStencilView;
   pDeviceFuncs->pfnCalcPrivateElementLayoutSize = CalcPrivateElementLayoutSize;
   pDeviceFuncs->pfnCreateElementLayout = CreateElementLayout;
   pDeviceFuncs->pfnDestroyElementLayout = DestroyElementLayout;
   pDeviceFuncs->pfnCalcPrivateBlendStateSize = CalcPrivateBlendStateSize;
   pDeviceFuncs->pfnCreateBlendState = CreateBlendState;
   pDeviceFuncs->pfnDestroyBlendState = DestroyBlendState;
   pDeviceFuncs->pfnCalcPrivateDepthStencilStateSize = CalcPrivateDepthStencilStateSize;
   pDeviceFuncs->pfnCreateDepthStencilState = CreateDepthStencilState;
   pDeviceFuncs->pfnDestroyDepthStencilState = DestroyDepthStencilState;
   pDeviceFuncs->pfnCalcPrivateRasterizerStateSize = CalcPrivateRasterizerStateSize;
   pDeviceFuncs->pfnCreateRasterizerState = CreateRasterizerState;
   pDeviceFuncs->pfnDestroyRasterizerState = DestroyRasterizerState;
   pDeviceFuncs->pfnCalcPrivateShaderSize = CalcPrivateShaderSize;
   pDeviceFuncs->pfnCreateVertexShader = CreateVertexShader;
   pDeviceFuncs->pfnCreateGeometryShader = CreateGeometryShader;
   pDeviceFuncs->pfnCreatePixelShader = CreatePixelShader;
   pDeviceFuncs->pfnCalcPrivateGeometryShaderWithStreamOutput = CalcPrivateGeometryShaderWithStreamOutput;
   pDeviceFuncs->pfnCreateGeometryShaderWithStreamOutput = CreateGeometryShaderWithStreamOutput;
   pDeviceFuncs->pfnDestroyShader = DestroyShader;
   pDeviceFuncs->pfnCalcPrivateSamplerSize = CalcPrivateSamplerSize;
   pDeviceFuncs->pfnCreateSampler = CreateSampler;
   pDeviceFuncs->pfnDestroySampler = DestroySampler;
   pDeviceFuncs->pfnCalcPrivateQuerySize = CalcPrivateQuerySize;
   pDeviceFuncs->pfnCreateQuery = CreateQuery;
   pDeviceFuncs->pfnDestroyQuery = DestroyQuery;
   pDeviceFuncs->pfnCheckFormatSupport = CheckFormatSupport;
   pDeviceFuncs->pfnCheckMultisampleQualityLevels = CheckMultisampleQualityLevels;
   pDeviceFuncs->pfnCheckCounterInfo = CheckCounterInfo;
   pDeviceFuncs->pfnCheckCounter = CheckCounter;
   pDeviceFuncs->pfnDestroyDevice = DestroyDevice;
   pDeviceFuncs->pfnSetTextFilterSize = SetTextFilterSize;
#if SUPPORT_D3D10_1
   if (pCreateData->Interface == D3D10_1_DDI_INTERFACE_VERSION ||
       pCreateData->Interface == D3D10_1_x_DDI_INTERFACE_VERSION ||
       pCreateData->Interface == D3D10_1_7_DDI_INTERFACE_VERSION) {
      D3D10_1DDI_DEVICEFUNCS *p10_1DeviceFuncs = pCreateData->p10_1DeviceFuncs;
      p10_1DeviceFuncs->pfnRelocateDeviceFuncs = RelocateDeviceFuncs1;
      p10_1DeviceFuncs->pfnCalcPrivateShaderResourceViewSize = CalcPrivateShaderResourceViewSize1;
      p10_1DeviceFuncs->pfnCreateShaderResourceView = CreateShaderResourceView1;
      p10_1DeviceFuncs->pfnCalcPrivateBlendStateSize = CalcPrivateBlendStateSize1;
      p10_1DeviceFuncs->pfnCreateBlendState = CreateBlendState1;
      p10_1DeviceFuncs->pfnResourceConvert = ResourceCopy;
      p10_1DeviceFuncs->pfnResourceConvertRegion = ResourceCopyRegion;
   }
#endif

#if SUPPORT_D3D11
   if (isD3D11) {
      D3D11DDI_DEVICEFUNCS *p11DeviceFuncs = pCreateData->p11DeviceFuncs;
      p11DeviceFuncs->pfnSetRenderTargets = SetRenderTargets11;
      p11DeviceFuncs->pfnRelocateDeviceFuncs = RelocateDeviceFuncs11;
      p11DeviceFuncs->pfnCalcPrivateResourceSize = CalcPrivateResourceSize11;
      p11DeviceFuncs->pfnCreateResource = CreateResource11;
      p11DeviceFuncs->pfnCalcPrivateShaderResourceViewSize = CalcPrivateShaderResourceViewSize11;
      p11DeviceFuncs->pfnCreateShaderResourceView = CreateShaderResourceView11;
      p11DeviceFuncs->pfnCalcPrivateDepthStencilViewSize = CalcPrivateDepthStencilViewSize11;
      p11DeviceFuncs->pfnCreateDepthStencilView = CreateDepthStencilView11;
      p11DeviceFuncs->pfnCalcPrivateBlendStateSize = CalcPrivateBlendStateSize1;
      p11DeviceFuncs->pfnCreateBlendState = CreateBlendState1;
      p11DeviceFuncs->pfnCalcPrivateGeometryShaderWithStreamOutput =
         CalcPrivateGeometryShaderWithStreamOutput11;
      p11DeviceFuncs->pfnCreateGeometryShaderWithStreamOutput =
         CreateGeometryShaderWithStreamOutput11;
      p11DeviceFuncs->pfnResourceConvert = ResourceCopy;
      p11DeviceFuncs->pfnResourceConvertRegion = ResourceCopyRegion;
   }
#endif

   /*
    * Fill in DXGI DDI functions
    */
   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnPresent =
      _Present;
   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnGetGammaCaps =
      _GetGammaCaps;
   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnSetDisplayMode =
      _SetDisplayMode;
   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnSetResourcePriority =
      _SetResourcePriority;
   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnQueryResourceResidency =
      _QueryResourceResidency;
   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnRotateResourceIdentities =
      _RotateResourceIdentities;
   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnBlt =
      _Blt;

   if (0) {
      return S_OK;
   } else {
      // Tell DXGI to not use the shared resource presentation path when
      // communicating with DWM:
      // http://msdn.microsoft.com/en-us/library/windows/hardware/ff569887(v=vs.85).aspx
      return DXGI_STATUS_NO_REDIRECTION;
   }
}


/*
 * ----------------------------------------------------------------------
 *
 * DestroyDevice --
 *
 *    The DestroyDevice function destroys a graphics context.
 *
 * ----------------------------------------------------------------------
 */

void APIENTRY
DestroyDevice(D3D10DDI_HDEVICE hDevice)   // IN
{
   unsigned i;

   LOG_ENTRYPOINT();

   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = pDevice->pipe;

   pipe->flush(pipe, NULL, 0);

   for (i = 0; i < PIPE_MAX_SO_BUFFERS; ++i) {
      pipe_so_target_reference(&pDevice->so_targets[i], NULL);
   }
   if (pDevice->draw_so_target) {
      pipe_so_target_reference(&pDevice->draw_so_target, NULL);
   }

   pipe->bind_fs_state(pipe, NULL);
   pipe->bind_vs_state(pipe, NULL);
   cso_unbind_context(pDevice->cso);
   cso_destroy_context(pDevice->cso);

   DeleteEmptyShader(pDevice, MESA_SHADER_FRAGMENT, pDevice->empty_fs);
   DeleteEmptyShader(pDevice, MESA_SHADER_VERTEX, pDevice->empty_vs);

   if (pDevice->default_blend_state)
      pipe->delete_blend_state(pipe, pDevice->default_blend_state);
   if (pDevice->default_depth_stencil_state) {
      pipe->delete_depth_stencil_alpha_state(
         pipe, pDevice->default_depth_stencil_state);
   }
   if (pDevice->default_rasterizer_state)
      pipe->delete_rasterizer_state(pipe, pDevice->default_rasterizer_state);

   util_unreference_framebuffer_state(&pDevice->fb);

   for (i = 0; i < PIPE_MAX_ATTRIBS; ++i) {
      if (!pDevice->vertex_buffers[i].is_user_buffer) {
         pipe_resource_reference(&pDevice->vertex_buffers[i].buffer.resource, NULL);
      }
   }

   pipe_resource_reference(&pDevice->index_buffer, NULL);

   static struct pipe_sampler_view *sampler_views[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   memset(sampler_views, 0, sizeof sampler_views);
   const mesa_shader_stage stages[] = {
      MESA_SHADER_FRAGMENT,
      MESA_SHADER_VERTEX,
      MESA_SHADER_GEOMETRY,
   };
   for (mesa_shader_stage stage : stages) {
      const unsigned max_views = MIN2(
            pipe->screen->shader_caps[stage].max_sampler_views,
            PIPE_MAX_SHADER_SAMPLER_VIEWS);
      if (max_views)
         pipe->set_sampler_views(pipe, stage, 0, 0, max_views, sampler_views);
   }

   pipe->destroy(pipe);
   pDevice->pipe = NULL;
   pDevice->screen->destroy(pDevice->screen);
   pDevice->screen = NULL;
}


/*
 * ----------------------------------------------------------------------
 *
 * RelocateDeviceFuncs --
 *
 *    The RelocateDeviceFuncs function notifies the user-mode
 *    display driver about the new location of the driver function table.
 *
 * ----------------------------------------------------------------------
 */

void APIENTRY
RelocateDeviceFuncs(D3D10DDI_HDEVICE hDevice,                           // IN
                    __in struct D3D10DDI_DEVICEFUNCS *pDeviceFunctions) // IN
{
   LOG_ENTRYPOINT();

   /*
    * Nothing to do as we don't store a pointer to this entity.
    */
}


#if SUPPORT_D3D10_1
/*
 * ----------------------------------------------------------------------
 *
 * RelocateDeviceFuncs1 --
 *
 *    The RelocateDeviceFuncs1 function notifies the user-mode
 *    display driver about the new location of the driver function table.
 *
 * ----------------------------------------------------------------------
 */

void APIENTRY
RelocateDeviceFuncs1(D3D10DDI_HDEVICE hDevice,                           // IN
                    __in struct D3D10_1DDI_DEVICEFUNCS *pDeviceFunctions) // IN
{
   LOG_ENTRYPOINT();

   /*
    * Nothing to do as we don't store a pointer to this entity.
    */
}
#endif


#if SUPPORT_D3D11
/*
 * The D3D11 feature-level 10 dispatch table extends the D3D10 table.  These
 * adapters translate the handful of entries whose public ABI changed; all
 * actual rendering remains in the common Gallium frontend.
 */
void APIENTRY
RelocateDeviceFuncs11(D3D10DDI_HDEVICE hDevice,
                      __in struct D3D11DDI_DEVICEFUNCS *pDeviceFunctions)
{
   LOG_ENTRYPOINT();
}


void APIENTRY
SetRenderTargets11(
   D3D10DDI_HDEVICE hDevice,
   const D3D10DDI_HRENDERTARGETVIEW *phRenderTargetView,
   UINT RTargets, UINT ClearTargets,
   D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
   const D3D11DDI_HUNORDEREDACCESSVIEW *phUnorderedAccessView,
   const UINT *pUAVInitialCounts,
   UINT UAVStartSlot, UINT NumUAVs, UINT UAVRangeStart, UINT UAVRangeSize)
{
   if (NumUAVs || UAVRangeSize) {
      SetError(hDevice, DXGI_DDI_ERR_UNSUPPORTED);
      return;
   }

   SetRenderTargets(hDevice, phRenderTargetView, RTargets, ClearTargets,
                    hDepthStencilView);
}


SIZE_T APIENTRY
CalcPrivateResourceSize11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
   D3D10DDIARG_CREATERESOURCE args = ResourceArgs10(pCreateResource);
   return CalcPrivateResourceSize(hDevice, &args);
}


void APIENTRY
CreateResource11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATERESOURCE *pCreateResource,
   D3D10DDI_HRESOURCE hResource,
   D3D10DDI_HRTRESOURCE hRTResource)
{
   if (!ResourceIsD3D10Compatible(pCreateResource)) {
      SetError(hDevice, DXGI_DDI_ERR_UNSUPPORTED);
      return;
   }

   D3D10DDIARG_CREATERESOURCE args = ResourceArgs10(pCreateResource);
   CreateResource(hDevice, &args, hResource, hRTResource);
}


SIZE_T APIENTRY
CalcPrivateShaderResourceViewSize11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATESHADERRESOURCEVIEW *pCreateShaderResourceView)
{
   D3D10_1DDIARG_CREATESHADERRESOURCEVIEW args =
      ShaderResourceViewArgs10_1(pCreateShaderResourceView);
   return CalcPrivateShaderResourceViewSize1(hDevice, &args);
}


void APIENTRY
CreateShaderResourceView11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATESHADERRESOURCEVIEW *pCreateShaderResourceView,
   D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
   D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView)
{
   if (pCreateShaderResourceView->ResourceDimension == D3D11DDIRESOURCE_BUFFEREX) {
      SetError(hDevice, DXGI_DDI_ERR_UNSUPPORTED);
      return;
   }

   D3D10_1DDIARG_CREATESHADERRESOURCEVIEW args =
      ShaderResourceViewArgs10_1(pCreateShaderResourceView);
   CreateShaderResourceView1(hDevice, &args, hShaderResourceView,
                             hRTShaderResourceView);
}


SIZE_T APIENTRY
CalcPrivateDepthStencilViewSize11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *pCreateDepthStencilView)
{
   D3D10DDIARG_CREATEDEPTHSTENCILVIEW args =
      DepthStencilViewArgs10(pCreateDepthStencilView);
   return CalcPrivateDepthStencilViewSize(hDevice, &args);
}


void APIENTRY
CreateDepthStencilView11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *pCreateDepthStencilView,
   D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
   D3D10DDI_HRTDEPTHSTENCILVIEW hRTDepthStencilView)
{
   if (pCreateDepthStencilView->Flags) {
      SetError(hDevice, DXGI_DDI_ERR_UNSUPPORTED);
      return;
   }

   D3D10DDIARG_CREATEDEPTHSTENCILVIEW args =
      DepthStencilViewArgs10(pCreateDepthStencilView);
   CreateDepthStencilView(hDevice, &args, hDepthStencilView,
                          hRTDepthStencilView);
}


SIZE_T APIENTRY
CalcPrivateGeometryShaderWithStreamOutput11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *pCreateData,
   const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures)
{
   if (!GeometryShaderWithStreamOutputIsD3D10Compatible(pCreateData))
      return 0;

   D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT args = {};
   args.pShaderCode = pCreateData->pShaderCode;
   args.NumEntries = pCreateData->NumEntries;
   args.StreamOutputStrideInBytes = pCreateData->NumStrides
      ? pCreateData->BufferStridesInBytes[0] : 0;
   return CalcPrivateGeometryShaderWithStreamOutput(hDevice, &args,
                                                     pSignatures);
}


void APIENTRY
CreateGeometryShaderWithStreamOutput11(
   D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *pCreateData,
   D3D10DDI_HSHADER hShader,
   D3D10DDI_HRTSHADER hRTShader,
   const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures)
{
   if (!GeometryShaderWithStreamOutputIsD3D10Compatible(pCreateData)) {
      SetError(hDevice, DXGI_DDI_ERR_UNSUPPORTED);
      return;
   }

   D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *entries = NULL;
   if (pCreateData->NumEntries) {
      entries = static_cast<D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *>(
         calloc(pCreateData->NumEntries, sizeof(*entries)));
      if (!entries) {
         SetError(hDevice, E_OUTOFMEMORY);
         return;
      }

      for (UINT i = 0; i < pCreateData->NumEntries; ++i) {
         entries[i].OutputSlot = pCreateData->pOutputStreamDecl[i].OutputSlot;
         entries[i].RegisterIndex = pCreateData->pOutputStreamDecl[i].RegisterIndex;
         entries[i].RegisterMask = pCreateData->pOutputStreamDecl[i].RegisterMask;
      }
   }

   D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT args = {};
   args.pShaderCode = pCreateData->pShaderCode;
   args.pOutputStreamDecl = entries;
   args.NumEntries = pCreateData->NumEntries;
   args.StreamOutputStrideInBytes = pCreateData->NumStrides
      ? pCreateData->BufferStridesInBytes[0] : 0;
   CreateGeometryShaderWithStreamOutput(hDevice, &args, hShader, hRTShader,
                                        pSignatures);
   free(entries);
}
#endif


/*
 * ----------------------------------------------------------------------
 *
 * Flush --
 *
 *    The Flush function submits outstanding hardware commands that
 *    are in the hardware command buffer to the display miniport driver.
 *
 * ----------------------------------------------------------------------
 */

void APIENTRY
Flush(D3D10DDI_HDEVICE hDevice)  // IN
{
   LOG_ENTRYPOINT();

   struct pipe_context *pipe = CastPipeContext(hDevice);

   pipe->flush(pipe, NULL, 0);
}


/*
 * ----------------------------------------------------------------------
 *
 * CheckFormatSupport --
 *
 *    The CheckFormatSupport function retrieves the capabilites that
 *    the device has with the specified format.
 *
 * ----------------------------------------------------------------------
 */

void APIENTRY
CheckFormatSupport(D3D10DDI_HDEVICE hDevice, // IN
                   DXGI_FORMAT Format,       // IN
                   __out UINT *pFormatCaps)  // OUT
{
   //LOG_ENTRYPOINT();

   struct pipe_context *pipe = CastPipeContext(hDevice);
   struct pipe_screen *screen = pipe->screen;

   *pFormatCaps = 0;

   enum pipe_format format = FormatTranslate(Format, false);
   if (format == PIPE_FORMAT_NONE) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
      return;
   }

   bool supported = false;

   if (Format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) {
      /*
       * We only need to support creation.
       * http://msdn.microsoft.com/en-us/library/windows/hardware/ff552818.aspx
       */
      return;
   }

   format = FormatTranslateSupported(screen, Format, false,
                                     PIPE_TEXTURE_2D, 0,
                                     PIPE_BIND_RENDER_TARGET);
   if (format != PIPE_FORMAT_NONE) {
      supported = true;
      *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET;
      *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;

      if (screen->is_format_supported(screen, format, PIPE_TEXTURE_2D, 4, 4,
                                      PIPE_BIND_RENDER_TARGET)) {
         *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
      }
   }

   format = FormatTranslateSupported(screen, Format, false,
                                     PIPE_TEXTURE_2D, 0,
                                     PIPE_BIND_SAMPLER_VIEW);
   if (format != PIPE_FORMAT_NONE) {
      supported = true;
      *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE;

#if SUPPORT_MSAA
      if (screen->is_format_supported(screen, format, PIPE_TEXTURE_2D, 4, 4,
                                      PIPE_BIND_RENDER_TARGET)) {
         *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD;
      }
#endif
   }

   format = FormatTranslateSupported(screen, Format, false,
                                     PIPE_BUFFER, 0,
                                     PIPE_BIND_VERTEX_BUFFER);
   if (format != PIPE_FORMAT_NONE) {
      supported = true;
      *pFormatCaps |= D3D11_1DDI_FORMAT_SUPPORT_VERTEX_BUFFER;
   }

   switch (Format) {
   case DXGI_FORMAT_R16_TYPELESS:
   case DXGI_FORMAT_D16_UNORM:
   case DXGI_FORMAT_R24G8_TYPELESS:
   case DXGI_FORMAT_D24_UNORM_S8_UINT:
   case DXGI_FORMAT_R32_TYPELESS:
   case DXGI_FORMAT_D32_FLOAT:
   case DXGI_FORMAT_R32G8X24_TYPELESS:
   case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      if (FormatTranslateSupported(screen, Format, true,
                                   PIPE_TEXTURE_2D, 0,
                                   PIPE_BIND_DEPTH_STENCIL) != PIPE_FORMAT_NONE)
         supported = true;
      break;
   default:
      break;
   }

   if (!supported)
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
}


/*
 * ----------------------------------------------------------------------
 *
 * CheckMultisampleQualityLevels --
 *
 *    The CheckMultisampleQualityLevels function retrieves the number
 *    of quality levels that the device supports for the specified
 *    number of samples.
 *
 * ----------------------------------------------------------------------
 */

void APIENTRY
CheckMultisampleQualityLevels(D3D10DDI_HDEVICE hDevice,        // IN
                              DXGI_FORMAT Format,              // IN
                              UINT SampleCount,                // IN
                              __out UINT *pNumQualityLevels)   // OUT
{
   //LOG_ENTRYPOINT();

   struct pipe_screen *screen = CastPipeContext(hDevice)->screen;
   *pNumQualityLevels = 0;
   if (!SampleCount || SampleCount > 32)
      return;

   enum pipe_format format = FormatTranslateSupported(screen, Format, false,
      PIPE_TEXTURE_2D, SampleCount, PIPE_BIND_RENDER_TARGET);
   if (format == PIPE_FORMAT_NONE)
      format = FormatTranslateSupported(screen, Format, true,
         PIPE_TEXTURE_2D, SampleCount, PIPE_BIND_DEPTH_STENCIL);
   if (format != PIPE_FORMAT_NONE)
      *pNumQualityLevels = 1;
}


/*
 * ----------------------------------------------------------------------
 *
 * SetTextFilterSize --
 *
 *    The SetTextFilterSize function sets the width and height
 *    of the monochrome convolution filter.
 *
 * ----------------------------------------------------------------------
 */

void APIENTRY
SetTextFilterSize(D3D10DDI_HDEVICE hDevice,  // IN
                  UINT Width,                // IN
                  UINT Height)               // IN
{
   LOG_ENTRYPOINT();

   LOG_UNSUPPORTED(Width != 1 || Height != 1);
}
