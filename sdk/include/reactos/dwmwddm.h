#ifndef _REACTOS_DWMWDDM_H_
#define _REACTOS_DWMWDDM_H_

#include <windef.h>
#include <winnt.h>

#define ROS_DWM_SHARED_SURFACE_MAGIC 0x5357444dU /* 'MDWS' */

typedef struct _ROS_DWM_SHARED_SURFACE_DESC
{
    ULONG Size;
    ULONG Magic;
    HWND hWnd;
    LUID AdapterLuid;
    /*
     * Optional shared-memory backing for windowed ICD present.
     * Keep the original header prefix stable and extend it with the
     * section-backed surface metadata that the WGL/ICD path consumes.
     */
    HANDLE SectionHandle;
    UINT Width;
    UINT Height;
    UINT Stride;
} ROS_DWM_SHARED_SURFACE_DESC, *PROS_DWM_SHARED_SURFACE_DESC;

#endif /* _REACTOS_DWMWDDM_H_ */
