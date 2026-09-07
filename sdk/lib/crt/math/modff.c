
/* Suppress the public header's x86 inline definition in this implementation. */
#ifndef _CRTBLD
#define _CRTBLD
#endif
#include <math.h>

_Check_return_
float
__cdecl
modff(
    _In_ float x,
    _Out_ float *y)
{
    double _Di, _Df;

    _Df = modf((double)x,&_Di);
    *y = (float)_Di;

    return (float)_Df;
}
