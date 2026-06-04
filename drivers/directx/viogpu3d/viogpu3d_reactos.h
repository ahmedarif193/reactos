/*
 * PROJECT:     ReactOS VioGpu3D
 * LICENSE:     BSD-3-Clause for upstream sample code,
 *              GPL-2.0-or-later for this build adaptation header
 * PURPOSE:     Force-included compatibility shim for the upstream viogpu3d
 *              sources when building inside the ReactOS tree.
 */

#ifndef _VIOGPU3D_REACTOS_H_
#define _VIOGPU3D_REACTOS_H_

/*
 * The upstream helper header includes winddi.h only for a small set of basic
 * GDI/DDI declarations. ReactOS' winddi.h pulls legacy DirectDraw headers by
 * default, which collides with the modern WDDM headers this driver also uses.
 */
#ifndef _NO_DDRAWINT_NO_COM
#define _NO_DDRAWINT_NO_COM 1
#endif

/*
 * On amd64 ReactOS provides inline EngSave/RestoreFloatingPointState bodies in
 * winddi.h when targeting Vista+, so avoid marking them dllimport.
 */
#ifndef _ENGINE_EXPORT_
#define _ENGINE_EXPORT_ 1
#endif

/*
 * Keep the upstream miniport on its newer callback contract even when the
 * rest of the tree is experimenting with older global WDDM targets.
 */
#ifdef DXGKDDI_INTERFACE_VERSION
#undef DXGKDDI_INTERFACE_VERSION
#endif
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM1_3

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

#ifndef __analysis_assume
#define __analysis_assume(expr)
#endif

#ifndef PAGE_WRITECOMBINE
#define PAGE_WRITECOMBINE 0x400
#endif

#endif /* _VIOGPU3D_REACTOS_H_ */
