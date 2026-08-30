
#include <math.h>

_Check_return_
double
__cdecl
exp2(
    _In_ double x)
{
    /* Prevent compilers from folding pow(2.0, x) back into exp2(x). */
    static const volatile double TWO = 2.0;
    return pow(TWO, x);
}
