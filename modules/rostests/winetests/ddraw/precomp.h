#ifndef _DDRAW_WINETEST_PRECOMP_H_
#define _DDRAW_WINETEST_PRECOMP_H_

#define COBJMACROS
#include <d3d.h>

/* ReactOS msvcrt exports exp2f(), but the public CRT math header does not declare it yet. */
float __cdecl exp2f(float value);

#endif /* _DDRAW_WINETEST_PRECOMP_H_ */
