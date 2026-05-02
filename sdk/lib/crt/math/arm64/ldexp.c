#include <math.h>

double
ldexp(double x, int exp)
{
    return __builtin_ldexp(x, exp);
}
