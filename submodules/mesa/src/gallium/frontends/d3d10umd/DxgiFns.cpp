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
 * DxgiFns.cpp --
 *    DXGI related functions.
 */

#include <stdio.h>

#include "DxgiFns.h"
#include "Format.h"
#include "State.h"

#include "Debug.h"

#include "util/format/u_format.h"

EXTERN_C bool
d3d10_rotate_resource_identities(struct pipe_context *pipe,
                                 struct pipe_resource *const *resources,
                                 void *const *runtime_resources,
                                 unsigned count);


/*
 * ----------------------------------------------------------------------
 *
 * _Present --
 *
 *    This is turned into kernel callbacks rather than directly emitted
 *    as fifo packets.
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
_Present(DXGI_DDI_ARG_PRESENT *pPresentData)
{

   LOG_ENTRYPOINT();

   struct Device *device = CastDevice(pPresentData->hDevice);
   Resource *pSrcResource = CastResource(pPresentData->hSurfaceToPresent);

   device->pipe->flush(device->pipe, NULL, 0);

   if (device->pDXGIBaseCallbacks &&
       device->pDXGIBaseCallbacks->pfnPresentCb &&
       device->hContext &&
       pSrcResource->allocation) {
      DXGIDDICB_PRESENT present = {};
      present.hSrcAllocation = pSrcResource->allocation;
      present.pDXGIContext = pPresentData->pDXGIContext;
      present.hContext = device->hContext;

      if (pPresentData->hDstResource) {
         Resource *pDstResource = CastResource(pPresentData->hDstResource);
         present.hDstAllocation = pDstResource->allocation;
      }

      return device->pDXGIBaseCallbacks->pfnPresentCb(device->hDevice,
                                                       &present);
   }

   device->pipe->screen->flush_frontbuffer(device->pipe->screen, device->pipe,
      pSrcResource->resource, 0, 0, pPresentData->pDXGIContext, 0, NULL);

   return S_OK;
}


/*
 * ----------------------------------------------------------------------
 *
 * _GetGammaCaps --
 *
 *    Return gamma capabilities.
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
_GetGammaCaps( DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *GetCaps )
{
   LOG_ENTRYPOINT();

   DXGI_GAMMA_CONTROL_CAPABILITIES *pCaps;

   pCaps = GetCaps->pGammaCapabilities;

   pCaps->ScaleAndOffsetSupported = false;
   pCaps->MinConvertedValue = 0.0;
   pCaps->MaxConvertedValue = 1.0;
   pCaps->NumGammaControlPoints = 17;

   for (UINT i = 0; i < pCaps->NumGammaControlPoints; i++) {
      pCaps->ControlPointPositions[i] = (float)i / (float)(pCaps->NumGammaControlPoints - 1);
   }

   return S_OK;
}


/*
 * ----------------------------------------------------------------------
 *
 * _SetDisplayMode --
 *
 *    Set the resource that is used to scan out to the display.
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
_SetDisplayMode( DXGI_DDI_ARG_SETDISPLAYMODE *SetDisplayMode )
{
   LOG_UNSUPPORTED_ENTRYPOINT();

   return S_OK;
}


/*
 * ----------------------------------------------------------------------
 *
 * _SetResourcePriority --
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
_SetResourcePriority( DXGI_DDI_ARG_SETRESOURCEPRIORITY *SetResourcePriority )
{
   LOG_ENTRYPOINT();

   /* ignore */

   return S_OK;
}


/*
 * ----------------------------------------------------------------------
 *
 * _QueryResourceResidency --
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
_QueryResourceResidency( DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *QueryResourceResidency )
{
   LOG_ENTRYPOINT();

   for (UINT i = 0; i < QueryResourceResidency->Resources; ++i) {
      QueryResourceResidency->pStatus[i] = DXGI_DDI_RESIDENCY_FULLY_RESIDENT;
   }

   return S_OK;
}


/*
 * ----------------------------------------------------------------------
 *
 * _RotateResourceIdentities --
 *
 *    Rotate the kernel and hardware identities of a list of resources.
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
_RotateResourceIdentities( DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *RotateResourceIdentities )
{
   LOG_ENTRYPOINT();

   if (!RotateResourceIdentities ||
       !RotateResourceIdentities->hDevice ||
       !RotateResourceIdentities->pResources ||
       RotateResourceIdentities->Resources < 2 ||
       RotateResourceIdentities->Resources > 16)
      return E_INVALIDARG;

   struct pipe_context *pipe = CastPipeDevice(RotateResourceIdentities->hDevice);
   if (!pipe)
      return E_INVALIDARG;

   Resource *resources[16];
   struct pipe_resource *pipe_resources[16];
   void *runtime_resources[16];

   for (UINT i = 0; i < RotateResourceIdentities->Resources; ++i) {
      resources[i] = CastResource(RotateResourceIdentities->pResources[i]);
      if (!resources[i] || !resources[i]->resource ||
          !resources[i]->allocation || !resources[i]->runtime_resource)
         return E_INVALIDARG;
      for (UINT previous = 0; previous < i; ++previous) {
         if (resources[previous] == resources[i] ||
             resources[previous]->allocation == resources[i]->allocation ||
             resources[previous]->runtime_resource ==
                resources[i]->runtime_resource)
            return E_INVALIDARG;
      }
      for (UINT subresource = 0;
           resources[i]->transfers &&
           subresource < resources[i]->NumSubResources;
           ++subresource) {
         if (resources[i]->transfers[subresource])
            return DXGI_ERROR_INVALID_CALL;
      }
      pipe_resources[i] = resources[i]->resource;
      runtime_resources[i] = resources[i]->runtime_resource;
   }

   /* Windows requires X,Y,Z to become Y,Z,X while the RT handles remain
    * fixed.  The target rotates the hardware backing in place so existing
    * Gallium views continue to name their original resource objects. */
   if (!d3d10_rotate_resource_identities(pipe, pipe_resources,
                                         runtime_resources,
                                         RotateResourceIdentities->Resources))
      return DXGI_ERROR_UNSUPPORTED;

   D3DKMT_HANDLE first_allocation = resources[0]->allocation;
   for (UINT i = 0; i + 1 < RotateResourceIdentities->Resources; ++i)
      resources[i]->allocation = resources[i + 1]->allocation;
   resources[RotateResourceIdentities->Resources - 1]->allocation =
      first_allocation;

   return S_OK;
}


/*
 * ----------------------------------------------------------------------
 *
 * _Blt --
 *
 *    Do a blt between two subresources. Apply MSAA resolve, format
 *    conversion and stretching.
 *
 * ----------------------------------------------------------------------
 */

HRESULT APIENTRY
_Blt(DXGI_DDI_ARG_BLT *Blt)
{
   LOG_UNSUPPORTED_ENTRYPOINT();

   return S_OK;
}
