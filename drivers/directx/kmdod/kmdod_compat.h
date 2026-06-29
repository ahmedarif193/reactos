/*
 * kmdod_compat.h — Minimal compatibility shims for building the Microsoft
 * KMDOD sample on ReactOS.  Most types are now in dispmprt.h/d3dkmdt.h.
 */
#pragma once

/* NonPagedPoolNx for C++ operator new */
#ifndef NonPagedPoolNx
#define NonPagedPoolNx ((POOL_TYPE)(NonPagedPool | 0x200))
#endif
