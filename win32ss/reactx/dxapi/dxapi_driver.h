#include <wdm.h>

#include <windef.h>
#include <winerror.h>
#include <wingdi.h>
#include <winddi.h>

#include <ddkmapi.h>

typedef VOID (NTAPI *PDXAPI_FUNCTION)(PVOID lpvInBuffer, PVOID lpvOutBuffer);

/* Prototypes */
VOID NTAPI DxGetVersionNumber(PVOID lpvInBuffer, LPDDGETVERSIONNUMBER lpvOutBuffer);
VOID NTAPI DxCloseHandle(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxOpenDirectDraw(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxOpenSurface(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxOpenVideoPort(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxGetKernelCaps(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxGetFieldNumber(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxSetFieldNumber(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxSetSkipPattern(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxGetSurfaceState(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxSetSurfaceState(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxLock(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxFlipOverlay(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxFlipVideoPort(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxGetCurrentAutoflip(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxGetPreviousAutoflip(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxRegisterEvent(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxUnregisterEvent(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxGetPolarity(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxOpenVpCatureDevice(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxAddVpCaptureBuffer(PVOID lpvInBuffer, PVOID lpvOutBuffer);
VOID NTAPI DxFlushVpCaptureBuffs(PVOID lpvInBuffer, PVOID lpvOutBuffer);

/* Internal driver table being use as looking up table for correct size of structs */
DWORD tblCheckInBuffer [] =
{
    /* DD_DXAPI_GETVERSIONNUMBER */
    0,
    /* DD_DXAPI_CLOSEHANDLE */
    sizeof(DDCLOSEHANDLE),
    0, // DD_DXAPI_OPENDIRECTDRAW
    0, // DD_DXAPI_OPENSURFACE
    0, // DD_DXAPI_OPENVIDEOPORT
    /* DD_DXAPI_GETKERNELCAPS */
    sizeof(HANDLE),
    /* DD_DXAPI_GET_VP_FIELD_NUMBER */
    sizeof(DDGETFIELDNUMIN),
    0, // DD_DXAPI_SET_VP_FIELD_NUMBER
    0, // DD_DXAPI_SET_VP_SKIP_FIELD
    0, // DD_DXAPI_GET_SURFACE_STATE
    0, // DD_DXAPI_SET_SURFACE_STATE
    0, // DD_DXAPI_LOCK
    /* DD_DXAPI_FLIP_OVERLAY */
    sizeof(DDFLIPOVERLAY),
    /* DD_DXAPI_FLIP_VP */
    sizeof(DDFLIPVIDEOPORT),
    /* DD_DXAPI_GET_CURRENT_VP_AUTOFLIP_SURFACE */
    sizeof(DDGETAUTOFLIPIN),
    /* DD_DXAPI_GET_LAST_VP_AUTOFLIP_SURFACE */
    sizeof(DDGETAUTOFLIPIN),
    /* DD_DXAPI_REGISTER_CALLBACK */
    sizeof(DDREGISTERCALLBACK),
    /* DD_DXAPI_UNREGISTER_CALLBACK */
    sizeof(DDREGISTERCALLBACK),
    /* DD_DXAPI_GET_POLARITY */
    sizeof(DDGETPOLARITYIN),
    /* DD_DXAPI_OPENVPCAPTUREDEVICE */
    sizeof(DDOPENVPCAPTUREDEVICEIN),
    /* DD_DXAPI_ADDVPCAPTUREBUFFER */
    sizeof(DDADDVPCAPTUREBUFF),
    /* DD_DXAPI_FLUSHVPCAPTUREBUFFERS */
    sizeof(HANDLE)
};

DWORD tblCheckOutBuffer [] =
{
    /* DD_DXAPI_GETVERSIONNUMBER */
    sizeof(DDGETVERSIONNUMBER),
    /* DD_DXAPI_CLOSEHANDLE */
    sizeof(DWORD),
    0, // DD_DXAPI_OPENDIRECTDRAW
    0, // DD_DXAPI_OPENSURFACE
    0, // DD_DXAPI_OPENVIDEOPORT
    /* DD_DXAPI_GETKERNELCAPS */
    sizeof(DDGETKERNELCAPSOUT),
    /* DD_DXAPI_GET_VP_FIELD_NUMBER */
    sizeof(DDGETFIELDNUMOUT),
    0, // DD_DXAPI_SET_VP_FIELD_NUMBER
    0, // DD_DXAPI_SET_VP_SKIP_FIELD
    0, // DD_DXAPI_GET_SURFACE_STATE
    0, // DD_DXAPI_SET_SURFACE_STATE
    0, // DD_DXAPI_LOCK
    /* DD_DXAPI_FLIP_OVERLAY */
    sizeof(DWORD),
    /* DD_DXAPI_FLIP_VP */
    sizeof(DWORD),
    /* DD_DXAPI_GET_CURRENT_VP_AUTOFLIP_SURFACE */
    sizeof(DDGETAUTOFLIPOUT),
    /* DD_DXAPI_GET_LAST_VP_AUTOFLIP_SURFACE */
    sizeof(DDGETAUTOFLIPOUT),
    /* DD_DXAPI_REGISTER_CALLBACK */
    sizeof(DWORD),
    /* DD_DXAPI_UNREGISTER_CALLBACK */
    sizeof(DWORD),
    /* DD_DXAPI_GET_POLARITY */
    sizeof(DDGETPOLARITYOUT),
    /* DD_DXAPI_OPENVPCAPTUREDEVICE */
    sizeof(DDOPENVPCAPTUREDEVICEOUT),
    /* DD_DXAPI_ADDVPCAPTUREBUFFER */
    sizeof(DWORD),
    /* DD_DXAPI_FLUSHVPCAPTUREBUFFERS */
    sizeof(DWORD)
};

/* Internal driver function */
DRVFN gDxApiEntryPoint [] = 
{
    {DD_DXAPI_GETVERSIONNUMBER - DD_FIRST_DXAPI, (PFN) DxGetVersionNumber},
    {DD_DXAPI_CLOSEHANDLE - DD_FIRST_DXAPI, (PFN) DxCloseHandle},
    {DD_DXAPI_OPENDIRECTDRAW - DD_FIRST_DXAPI, (PFN) DxOpenDirectDraw},
    {DD_DXAPI_OPENSURFACE - DD_FIRST_DXAPI, (PFN) DxOpenSurface},
    {DD_DXAPI_OPENVIDEOPORT - DD_FIRST_DXAPI, (PFN) DxOpenVideoPort},
    {DD_DXAPI_GETKERNELCAPS - DD_FIRST_DXAPI, (PFN) DxGetKernelCaps},
    {DD_DXAPI_GET_VP_FIELD_NUMBER - DD_FIRST_DXAPI, (PFN) DxGetFieldNumber},
    {DD_DXAPI_SET_VP_FIELD_NUMBER - DD_FIRST_DXAPI, (PFN) DxSetFieldNumber},
    {DD_DXAPI_SET_VP_SKIP_FIELD - DD_FIRST_DXAPI, (PFN) DxSetSkipPattern},
    {DD_DXAPI_GET_SURFACE_STATE - DD_FIRST_DXAPI, (PFN) DxGetSurfaceState},
    {DD_DXAPI_SET_SURFACE_STATE - DD_FIRST_DXAPI, (PFN) DxSetSurfaceState},
    {DD_DXAPI_LOCK - DD_FIRST_DXAPI, (PFN) DxLock},
    {DD_DXAPI_FLIP_OVERLAY - DD_FIRST_DXAPI, (PFN) DxFlipOverlay},
    {DD_DXAPI_FLIP_VP - DD_FIRST_DXAPI, (PFN) DxFlipVideoPort},
    {DD_DXAPI_GET_CURRENT_VP_AUTOFLIP_SURFACE - DD_FIRST_DXAPI, (PFN) DxGetCurrentAutoflip},
    {DD_DXAPI_GET_LAST_VP_AUTOFLIP_SURFACE - DD_FIRST_DXAPI, (PFN) DxGetPreviousAutoflip},
    {DD_DXAPI_REGISTER_CALLBACK - DD_FIRST_DXAPI, (PFN) DxRegisterEvent},
    {DD_DXAPI_UNREGISTER_CALLBACK - DD_FIRST_DXAPI, (PFN) DxUnregisterEvent},
    {DD_DXAPI_GET_POLARITY - DD_FIRST_DXAPI, (PFN) DxGetPolarity},
    {DD_DXAPI_OPENVPCAPTUREDEVICE - DD_FIRST_DXAPI, (PFN) DxOpenVpCatureDevice},
    {DD_DXAPI_ADDVPCAPTUREBUFFER - DD_FIRST_DXAPI, (PFN) DxAddVpCaptureBuffer},
    {DD_DXAPI_FLUSHVPCAPTUREBUFFERS - DD_FIRST_DXAPI, (PFN) DxFlushVpCaptureBuffs}
};
