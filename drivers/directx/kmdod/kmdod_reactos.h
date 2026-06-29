/*
 * PROJECT:     ReactOS KMDOD (Kernel Mode Display-Only Driver)
 * LICENSE:     MS-PL (Microsoft Public License) -- original sample code
 *              GPL-2.0-or-later for ReactOS adaptation layer
 * PURPOSE:     ReactOS build adaptation header.
 *              Force-included before all KMDOD .cxx files via compiler flag.
 *              ONLY #define macros allowed here -- no inline functions, no
 *              type references -- because this is processed before any
 *              system header.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 */

#ifndef _KMDOD_REACTOS_H_
#define _KMDOD_REACTOS_H_

/* ---- Target Windows 8 (WDDM 1.2) for DOD support ---------------------- */
#undef  _WIN32_WINNT
#define _WIN32_WINNT    0x0602          /* Windows 8 */
#undef  WINVER
#define WINVER          0x0602
#undef  NTDDI_VERSION
#define NTDDI_VERSION   0x06020000      /* NTDDI_WIN8 */

/* ---- DXGKDDI interface version for Win8.1 DOD -------------------------- */
/* KMDOD sample uses Offset0/90/180/270 rotation fields which require
 * WDDM 1.3 path-independent rotation (0x4003).  Use WDDM1_3 to include
 * all the fields the sample expects. */
#undef  DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION   0x4003  /* DXGKDDI_INTERFACE_VERSION_WDDM1_3_PATH_INDEPENDENT_ROTATION */

/* ---- NonPagedPoolNx ---------------------------------------------------- */
/*
 * ReactOS does not yet define NonPagedPoolNx in its POOL_TYPE enum.
 * Map it to NonPagedPool since NX pools are not yet implemented.
 */
#ifndef NonPagedPoolNx
#define NonPagedPoolNx  NonPagedPool
#endif

/* ---- __analysis_assume ------------------------------------------------- */
#ifndef __analysis_assume
#define __analysis_assume(expr)
#endif

/* ---- PAGE_WRITECOMBINE ------------------------------------------------- */
#ifndef PAGE_WRITECOMBINE
#define PAGE_WRITECOMBINE 0x400
#endif

/* ---- ARRAYSIZE for C++ ------------------------------------------------- */
/* ReactOS ntdef.h picks the __GNUC__ path for RTL_NUMBER_OF_V2 which uses
 * typeof — a C-only GCC extension that fails in C++.  We cannot override
 * it here (ntdef.h is included later), so we define a simple KMDOD_ARRAYSIZE
 * and patch the source. */

#endif /* _KMDOD_REACTOS_H_ */
