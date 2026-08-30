
#include <math.h>

_Check_return_
float
__cdecl
exp2f(
    _In_ float x)
{
    /* Prevent compilers from folding powf(2.0f, x) back into exp2f(x). */
    static const volatile float TWO = 2.0f;
    return powf(TWO, x);
}
