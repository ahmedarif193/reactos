#ifndef _DDRAW_WINETEST_PRECOMP_H_
#define _DDRAW_WINETEST_PRECOMP_H_

#define COBJMACROS
#include <d3d.h>

/* ReactOS' legacy d3dhal.h exposes only the Direct3D 1 device descriptor alias. */
typedef D3DDEVICEDESC2 D3DDEVICEDESC_V2;

/* ReactOS msvcrt exports exp2f(), but the public CRT math header does not declare it yet. */
float __cdecl exp2f(float value);

#endif /* _DDRAW_WINETEST_PRECOMP_H_ */
