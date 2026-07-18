#ifndef _D3D8_WINETEST_PRECOMP_H_
#define _D3D8_WINETEST_PRECOMP_H_

#define COBJMACROS
#include <d3d8.h>

/* ReactOS' legacy d3d8.h spells this C vtable member with the wrong case. */
#undef IDirect3DDevice8_ProcessVertices
#define IDirect3DDevice8_ProcessVertices(p,a,b,c,d,e) (p)->lpVtbl->ProcessVertices(p,a,b,c,d,e)

#endif /* _D3D8_WINETEST_PRECOMP_H_ */
